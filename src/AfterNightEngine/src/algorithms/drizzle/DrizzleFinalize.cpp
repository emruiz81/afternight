// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - drizzle finalization: each destination band's terms into output samples,
// the peak-normalized sum's normalization, and the metadata transition of a completed result.
//
// A band is finalized as soon as its last frame is accumulated, over disjoint output rows on the
// operation's workers. The division by the signed total weight happens here, and so does the
// mapping of a normalized integer input's full scale into the floating output: accumulation keeps
// its terms in the source representation's code units, and dividing the mean by the
// representation's positive maximum puts it on the output's scale without requantizing. Samples in
// any other numeric domain, and floating samples, are not rescaled. A peak-normalized sum cannot be
// narrowed band by band, because its divisor is the largest numerator of the whole output; its
// Float64 numerators are kept until every band is done and are then narrowed once.

#include "algorithms/drizzle/DrizzleExecutionInternal.h"

#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <cmath>
#include <limits>
#include <string>

namespace an
{
namespace drizzle_detail
{
Status executionFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = "an::executeDrizzle", .domain = ErrorDomain::Algorithms});
}

namespace
{
// The divisor that maps a normalized or encoded integer representation's full scale to one: its
// positive maximum. One for every other numeric domain and for floating samples.
double normalizedDivisor(const ImageDescriptor& descriptor) noexcept
{
    if (descriptor.numericDomain() != NumericDomain::NormalizedLinear &&
        descriptor.numericDomain() != NumericDomain::EncodedNonlinear)
        return 1.0;
    switch (descriptor.sampleType())
    {
    case SampleType::UInt8:
        return kSampleMaximumOf<std::uint8_t>;
    case SampleType::Int8:
        return kSampleMaximumOf<std::int8_t>;
    case SampleType::UInt16:
        return kSampleMaximumOf<std::uint16_t>;
    case SampleType::Int16:
        return kSampleMaximumOf<std::int16_t>;
    case SampleType::UInt32:
        return kSampleMaximumOf<std::uint32_t>;
    case SampleType::Int32:
        return kSampleMaximumOf<std::int32_t>;
    case SampleType::Float32:
    case SampleType::Float64:
        return 1.0;
    }
    return 1.0;
}
} // namespace

Status finalizeBand(const ExecutionContext& execution, const DrizzleModel& model, const DrizzleAccumulatorBand& band,
                    DrizzleResult& result, std::span<double> numerators)
{
    const auto& extent = result.descriptor.extent();
    const auto mask = *result.mask.view();
    const bool single = result.descriptor.sampleType() == SampleType::Float32;
    const double maximum = single ? (std::numeric_limits<float>::max)() : (std::numeric_limits<double>::max)();
    const auto bandPlane = static_cast<std::size_t>(extent.width()) * band.rowCount;
    const auto outputPlane = static_cast<std::size_t>(extent.width()) * extent.height();
    const double divisor = normalizedDivisor(model.request().input);
    auto* weightMap = static_cast<float*>(result.weightMap.data());
    auto* coverage = static_cast<std::uint32_t*>(result.coverageMap.data());
    std::atomic<std::uint64_t> validCount{0u}, invalidCount{0u};
    std::mutex maximumMutex;
    double maximumWeight = 0.0;
    const auto body = [&](TaskRange range) -> Status {
        std::uint64_t validSamples = 0u, invalidSamples = 0u;
        double localMaximum = 0.0;
        for (auto flatRow = range.begin; flatRow < range.end; ++flatRow)
        {
            const auto channel = static_cast<std::uint32_t>(flatRow / band.rowCount);
            const auto row = static_cast<std::uint32_t>(flatRow % band.rowCount);
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            auto bits = *mask.row(channel, band.firstRow + row);
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                if (x % 4096u == 0u)
                    if (const auto stopped = execution.checkCancellation(); stopped.failed())
                        return stopped;
                const auto index = channel * bandPlane + static_cast<std::size_t>(row) * extent.width() + x;
                const auto target =
                    channel * outputPlane + static_cast<std::size_t>(band.firstRow + row) * extent.width() + x;
                const double weight = band.weightSums[index];
                if (!std::isfinite(weight) || !std::isfinite(band.sums[index]))
                    return executionFailure(ErrorCode::InvalidData, "non-finite completed drizzle terms");
                const bool valid = weight > 0.0;
                const double value =
                    valid ? (numerators.empty() ? band.sums[index] / weight : band.sums[index]) / divisor : 0.0;
                if (!std::isfinite(value) || (numerators.empty() && std::abs(value) > maximum))
                    return executionFailure(ErrorCode::InvalidData, "drizzle mean is not representable in the output");
                if (weightMap != nullptr)
                {
                    if (std::abs(weight) > (std::numeric_limits<float>::max)())
                        return executionFailure(ErrorCode::InvalidData, "requested weight map is not representable");
                    weightMap[target] = static_cast<float>(weight);
                }
                if (coverage != nullptr)
                    coverage[target] = band.coverage[index];
                if (!numerators.empty())
                    numerators[target] = value;
                else if (single)
                    static_cast<float*>(result.image.data())[target] = static_cast<float>(value);
                else
                    static_cast<double*>(result.image.data())[target] = value;
                if (valid)
                {
                    bits[x / 8u] |= static_cast<std::uint8_t>(1u << (x % 8u));
                    ++validSamples;
                }
                else
                    ++invalidSamples;
                if (result.report.maximumWeight)
                    localMaximum = (std::max)(localMaximum, weight);
            }
        }
        validCount.fetch_add(validSamples, std::memory_order_relaxed);
        invalidCount.fetch_add(invalidSamples, std::memory_order_relaxed);
        if (result.report.maximumWeight)
        {
            const std::lock_guard lock(maximumMutex);
            maximumWeight = (std::max)(maximumWeight, localMaximum);
        }
        return Status::success();
    };
    const auto status = parallelFor(
        execution, static_cast<std::uint64_t>(extent.channelCount()) * band.rowCount,
        {static_cast<std::uint64_t>(extent.width()) * 32u, static_cast<std::uint64_t>(extent.width()) * 32u}, body);
    if (status.failed())
        return status;
    result.report.validOutputSamples += validCount.load();
    result.report.invalidOutputSamples += invalidCount.load();
    if (result.report.maximumWeight)
        *result.report.maximumWeight = (std::max)(*result.report.maximumWeight, maximumWeight);
    return execution.checkCancellation();
}
Status normalizeSum(const ExecutionContext& execution, std::span<const double> numerators, DrizzleResult& result)
{
    // Independent maxima avoid a dependency through every sample. All terms were checked finite
    // during band finalization, so their merge has no rounding-sensitive order and needs no scratch.
    std::array<double, 4u> peaks{};
    const auto bulk = numerators.size() - numerators.size() % peaks.size();
    for (std::size_t i = 0u; i < bulk; i += peaks.size())
    {
        if (i % 4096u == 0u)
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
        peaks[0] = (std::max)(peaks[0], numerators[i]);
        peaks[1] = (std::max)(peaks[1], numerators[i + 1u]);
        peaks[2] = (std::max)(peaks[2], numerators[i + 2u]);
        peaks[3] = (std::max)(peaks[3], numerators[i + 3u]);
    }
    double peak = (std::max)((std::max)(peaks[0], peaks[1]), (std::max)(peaks[2], peaks[3]));
    for (auto i = bulk; i < numerators.size(); ++i)
        peak = (std::max)(peak, numerators[i]);
    const bool single = result.descriptor.sampleType() == SampleType::Float32;
    const auto body = [&](TaskRange range) -> Status {
        for (auto i = range.begin; i < range.end; ++i)
        {
            if (i % 4096u == 0u)
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
            const double value = peak > 0.0 ? std::clamp(numerators[i] / peak, 0.0, 1.0) : numerators[i];
            if (single)
            {
                if (std::abs(value) > (std::numeric_limits<float>::max)())
                    return executionFailure(ErrorCode::InvalidData, "drizzle sum is not representable in the output");
                static_cast<float*>(result.image.data())[i] = static_cast<float>(value);
            }
            else
                static_cast<double*>(result.image.data())[i] = value;
        }
        return Status::success();
    };
    return parallelFor(execution, numerators.size(), {single ? 12u : 16u, 8u}, body);
}
} // namespace drizzle_detail

Result<MetadataTransition> drizzleTransition(const DrizzleModel& model, const ImageSemantics& source)
{
    auto request = source.toRequest();
    request.processingState.registered = true;
    request.processingState.normalized = request.processingState.normalized ||
                                         model.request().output.combination == DrizzleCombination::PeakNormalizedSum;
    request.processingState.demosaiced = request.processingState.demosaiced || model.usesCfa();
    request.validity = drizzleOperationEffect().validity.produced;
    auto semantics = ImageSemantics::create(request);
    if (!semantics.hasValue())
        return semantics.status();
    return operationTransition(
        drizzleOperationEffect(),
        {.operation = "an::executeDrizzle",
         .detail = std::string("kernel=") + drizzleKernelName(model.request().parameters.kernel) +
                   ",frames=" + std::to_string(model.request().frameCount) +
                   ",scale=" + std::to_string(model.request().parameters.scale) +
                   ",pixelFraction=" + std::to_string(model.request().parameters.pixelFraction) + ",combination=" +
                   (model.request().output.combination == DrizzleCombination::PeakNormalizedSum ? "PeakNormalizedSum"
                                                                                                : "WeightedMean"),
         .semantics = *semantics});
}
} // namespace an

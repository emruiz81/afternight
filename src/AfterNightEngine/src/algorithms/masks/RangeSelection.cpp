// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - soft normalized range-selection images.
//
// Two walks produce the same weights. When every sample is admitted and source and destination
// share one contiguous layout, the image is one flat run of samples: it is split by sample index,
// a Float32 image reaches the compiled kernel when the dispatch table has one, and otherwise a scalar
// body specialised at compile time on fuzziness, screening and inversion runs with no per-sample
// branch on the request. Every other case - strides that differ, a validity rule, a mask - walks
// rows, reading each channel through its own plane and pixel step and leaving an excluded sample's
// destination untouched.
//
// A non-finite sample that is admitted is `ErrorCode::InvalidData` rather than a weight. The flat walk
// tests the exponent bits and reports after writing its range; the row walk tests before it writes
// the sample.

#include <AfterNightEngine/Algorithms/Masks/RangeSelection.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/AlgorithmKernels.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::rangeSelectImage";

[[nodiscard]] Status rangeFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

[[nodiscard]] bool isSampleAligned(const void* samples, std::uint32_t alignment) noexcept
{
    return reinterpret_cast<std::uintptr_t>(samples) % alignment == 0u;
}

[[nodiscard]] bool exactlyAliases(const void* sourceSamples, const ImageLayout& sourceLayout,
                                  const void* destinationSamples, const ImageLayout& destinationLayout) noexcept
{
    return sourceSamples == destinationSamples && sourceLayout == destinationLayout;
}

[[nodiscard]] bool maskAccepts(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    if (validity.mask == nullptr)
    {
        return true;
    }
    const ValidityMaskView<const std::uint8_t>& mask = *validity.mask;
    const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

[[nodiscard]] Status validateValidity(const KernelValidityView& validity, const ImageLayout& sourceLayout,
                                      const void* destinationSamples, const ImageLayout& destinationLayout)
{
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return rangeFailure(ErrorCode::InvalidArgument,
                                "range-selection validity supplies a mask for a rule that does not use one");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return rangeFailure(ErrorCode::InvalidArgument, "explicit-mask range-selection validity supplies no mask");
    }
    if (validity.mask->width() != sourceLayout.extent().width() ||
        validity.mask->height() != sourceLayout.extent().height() || validity.maskPlane >= validity.mask->planeCount())
    {
        return rangeFailure(ErrorCode::IncompatibleArguments,
                            "the range-selection validity mask does not cover the image and selected plane");
    }
    if (regionsOverlap(validity.mask->data(), validity.mask->spannedByteCount(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()))
    {
        return rangeFailure(ErrorCode::InvalidArgument,
                            "the range-selection validity mask overlaps the writable destination");
    }
    return Status::success();
}

template <typename SampleT>
[[nodiscard]] const SampleT* channelRow(const void* samples, const ImageLayout& layout, std::uint32_t channel,
                                        std::uint32_t y) noexcept
{
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(samples);
    return reinterpret_cast<const SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

template <typename SampleT>
[[nodiscard]] SampleT* channelRow(void* samples, const ImageLayout& layout, std::uint32_t channel,
                                  std::uint32_t y) noexcept
{
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(samples);
    return reinterpret_cast<SampleT*>(bytes + *layout.rowByteOffset(channel / layout.samplesPerPixel(), y));
}

struct RangeWeightPlan
{
    double lowerLimit = 0.0;
    double upperLimit = 0.0;
    double ramp = 0.0;
    double inverseRamp = 0.0;
    double lowerPlateau = 0.0;
    double upperPlateau = 0.0;
    bool screening = false;
    bool invert = false;
};

[[nodiscard]] RangeWeightPlan planWeights(const RangeSelectionRequest& request) noexcept
{
    const double ramp = (request.upperLimit - request.lowerLimit) * request.fuzziness * 0.5;
    return {.lowerLimit = request.lowerLimit,
            .upperLimit = request.upperLimit,
            .ramp = ramp,
            .inverseRamp = ramp != 0.0 ? 1.0 / ramp : 0.0,
            .lowerPlateau = request.lowerLimit + ramp,
            .upperPlateau = request.upperLimit - ramp,
            .screening = request.screening,
            .invert = request.invert};
}

[[nodiscard]] double selectedWeight(double value, const RangeWeightPlan& plan) noexcept
{
    double weight = 0.0;
    if (value >= plan.lowerLimit && value <= plan.upperLimit)
    {
        if (plan.ramp == 0.0)
        {
            weight = 1.0;
        }
        else
        {
            if (value < plan.lowerPlateau)
            {
                weight = (value - plan.lowerPlateau) * plan.inverseRamp + 1.0;
            }
            else if (value > plan.upperPlateau)
            {
                weight = (plan.upperPlateau - value) * plan.inverseRamp + 1.0;
            }
            else
            {
                weight = 1.0;
            }
        }
    }
    if (plan.screening)
    {
        weight *= value;
    }
    return plan.invert ? 1.0 - weight : weight;
}

template <bool Screening, bool Invert>
[[nodiscard]] double selectedFuzzyWeight(double value, const RangeWeightPlan& plan) noexcept
{
    double weight = 0.0;
    if (value >= plan.lowerLimit && value <= plan.upperLimit)
    {
        if (value < plan.lowerPlateau)
        {
            weight = (value - plan.lowerPlateau) * plan.inverseRamp + 1.0;
        }
        else if (value > plan.upperPlateau)
        {
            weight = (plan.upperPlateau - value) * plan.inverseRamp + 1.0;
        }
        else
        {
            weight = 1.0;
        }
    }
    if constexpr (Screening)
    {
        weight *= value;
    }
    if constexpr (Invert)
    {
        weight = 1.0 - weight;
    }
    return weight;
}

template <bool Screening, bool Invert>
[[nodiscard]] double selectedHardWeight(double value, const RangeWeightPlan& plan) noexcept
{
    double weight = value >= plan.lowerLimit && value <= plan.upperLimit ? 1.0 : 0.0;
    if constexpr (Screening)
    {
        weight *= value;
    }
    if constexpr (Invert)
    {
        weight = 1.0 - weight;
    }
    return weight;
}

template <ResidentSample SampleT, bool Screening, bool Invert, bool Fuzzy>
[[nodiscard]] Result<RangeSelectionResult>
selectContiguousAllValid(const ExecutionContext& execution, const SampleT* source, SampleT* destination,
                         std::uint64_t sampleCount, std::uint32_t channels, const RangeWeightPlan& weights)
{
    const TaskCost cost{.bytesPerItem = sizeof(SampleT) * 2u, .operationsPerItem = Fuzzy ? 8u : 3u};
    if constexpr (std::is_same_v<SampleT, float>)
    {
        const RangeSelectionFloat32RunFn run = rangeSelectionFloat32RunFor(execution);
        if (run != nullptr)
        {
            std::atomic<std::uint64_t> nonFinite{0u};
            const RangeSelectionRunParameters parameters{.lowerLimit = weights.lowerLimit,
                                                         .upperLimit = weights.upperLimit,
                                                         .inverseRamp = weights.inverseRamp,
                                                         .lowerPlateau = weights.lowerPlateau,
                                                         .upperPlateau = weights.upperPlateau,
                                                         .screening = Screening,
                                                         .invert = Invert,
                                                         .fuzzy = Fuzzy};
            const Status vectorized = parallelFor(execution, sampleCount, cost, [&](TaskRange range) -> Status {
                nonFinite.fetch_add(
                    run(source + range.begin, destination + range.begin, range.end - range.begin, parameters),
                    std::memory_order_relaxed);
                return Status::success();
            });
            if (vectorized.failed())
            {
                return vectorized;
            }
            if (nonFinite.load(std::memory_order_relaxed) != 0u)
            {
                return rangeFailure(ErrorCode::InvalidData, "range selection admitted a sample that is not finite");
            }
            RangeSelectionResult result;
            result.run.selection = ValidityCount{.valid = sampleCount, .total = sampleCount};
            result.run.outputsWritten = sampleCount;
            result.channelCount = channels;
            return result;
        }
    }

    const Status walked = parallelFor(execution, sampleCount, cost, [&](TaskRange range) -> Status {
        std::uint64_t nonFinite = 0u;
        for (std::uint64_t index = range.begin; index < range.end; ++index)
        {
            if constexpr (std::is_same_v<SampleT, float>)
            {
                const std::uint32_t bits = std::bit_cast<std::uint32_t>(source[index]);
                nonFinite |= (bits & 0x7f800000u) == 0x7f800000u ? 1u : 0u;
            }
            else
            {
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(source[index]);
                nonFinite |= (bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull ? 1u : 0u;
            }
            const double value = static_cast<double>(source[index]);
            if constexpr (Fuzzy)
            {
                destination[index] = static_cast<SampleT>(selectedFuzzyWeight<Screening, Invert>(value, weights));
            }
            else
            {
                destination[index] = static_cast<SampleT>(selectedHardWeight<Screening, Invert>(value, weights));
            }
        }
        return nonFinite == 0u
                   ? Status::success()
                   : rangeFailure(ErrorCode::InvalidData, "range selection admitted a sample that is not finite");
    });
    if (walked.failed())
    {
        return walked;
    }

    RangeSelectionResult result;
    result.run.selection = ValidityCount{.valid = sampleCount, .total = sampleCount};
    result.run.outputsWritten = sampleCount;
    result.channelCount = channels;
    return result;
}

template <ResidentSample SampleT>
[[nodiscard]] Result<RangeSelectionResult>
dispatchContiguousAllValid(const ExecutionContext& execution, const SampleT* source, SampleT* destination,
                           std::uint64_t sampleCount, std::uint32_t channels, const RangeWeightPlan& weights)
{
    if (weights.ramp != 0.0)
    {
        if (weights.screening)
        {
            return weights.invert ? selectContiguousAllValid<SampleT, true, true, true>(execution, source, destination,
                                                                                        sampleCount, channels, weights)
                                  : selectContiguousAllValid<SampleT, true, false, true>(
                                        execution, source, destination, sampleCount, channels, weights);
        }
        return weights.invert ? selectContiguousAllValid<SampleT, false, true, true>(execution, source, destination,
                                                                                     sampleCount, channels, weights)
                              : selectContiguousAllValid<SampleT, false, false, true>(execution, source, destination,
                                                                                      sampleCount, channels, weights);
    }
    if (weights.screening)
    {
        return weights.invert ? selectContiguousAllValid<SampleT, true, true, false>(execution, source, destination,
                                                                                     sampleCount, channels, weights)
                              : selectContiguousAllValid<SampleT, true, false, false>(execution, source, destination,
                                                                                      sampleCount, channels, weights);
    }
    return weights.invert ? selectContiguousAllValid<SampleT, false, true, false>(execution, source, destination,
                                                                                  sampleCount, channels, weights)
                          : selectContiguousAllValid<SampleT, false, false, false>(execution, source, destination,
                                                                                   sampleCount, channels, weights);
}

template <ResidentSample SampleT>
[[nodiscard]] Result<RangeSelectionResult>
selectTyped(const ExecutionContext& execution, const void* sourceSamples, const ImageLayout& sourceLayout,
            void* destinationSamples, const ImageLayout& destinationLayout, const RangeSelectionRequest& request,
            const KernelValidityView& validity)
{
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    const std::uint32_t sourceStep = sourceLayout.samplesPerPixel();
    const std::uint32_t destinationStep = destinationLayout.samplesPerPixel();
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels * sizeof(SampleT) * 2u,
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels * 8u};
    const RangeWeightPlan weights = planWeights(request);
    const bool acceptsEverySample = validity.descriptor.rule() == ValidityRule::AllValid && validity.mask == nullptr;
    if (acceptsEverySample && sourceLayout == destinationLayout && sourceLayout.isContiguous())
    {
        const std::uint64_t sampleCount = static_cast<std::uint64_t>(width) * height * channels;
        return dispatchContiguousAllValid(execution, static_cast<const SampleT*>(sourceSamples),
                                          static_cast<SampleT*>(destinationSamples), sampleCount, channels, weights);
    }
    std::atomic<std::uint64_t> admitted{0u};

    const Status walked = parallelFor(execution, height, cost, [&](TaskRange range) -> Status {
        std::uint64_t localAdmitted = 0u;
        for (std::uint64_t y64 = range.begin; y64 < range.end; ++y64)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(y64);
            if (acceptsEverySample)
            {
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    const SampleT* const source = channelRow<SampleT>(sourceSamples, sourceLayout, channel, y) +
                                                  channel % sourceLayout.samplesPerPixel();
                    SampleT* const destination =
                        channelRow<SampleT>(destinationSamples, destinationLayout, channel, y) +
                        channel % destinationLayout.samplesPerPixel();
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const double value = static_cast<double>(source[static_cast<std::size_t>(x) * sourceStep]);
                        if (!std::isfinite(value))
                        {
                            return rangeFailure(ErrorCode::InvalidData,
                                                "range selection admitted a sample that is not finite");
                        }
                        destination[static_cast<std::size_t>(x) * destinationStep] =
                            static_cast<SampleT>(selectedWeight(value, weights));
                    }
                }
                localAdmitted += static_cast<std::uint64_t>(width) * channels;
                continue;
            }
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                const SampleT* const source = channelRow<SampleT>(sourceSamples, sourceLayout, channel, y) +
                                              channel % sourceLayout.samplesPerPixel();
                SampleT* const destination = channelRow<SampleT>(destinationSamples, destinationLayout, channel, y) +
                                             channel % destinationLayout.samplesPerPixel();
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const SampleT stored = source[static_cast<std::size_t>(x) * sourceStep];
                    if (!maskAccepts(validity, x, y) || !sampleIsValidByValue(validity.descriptor, stored))
                    {
                        continue;
                    }
                    const double value = static_cast<double>(stored);
                    if (!std::isfinite(value))
                    {
                        return rangeFailure(ErrorCode::InvalidData,
                                            "range selection admitted a sample that is not finite");
                    }
                    destination[static_cast<std::size_t>(x) * destinationStep] =
                        static_cast<SampleT>(selectedWeight(value, weights));
                    ++localAdmitted;
                }
            }
        }
        admitted.fetch_add(localAdmitted, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }

    const std::uint64_t total = static_cast<std::uint64_t>(width) * height * channels;
    RangeSelectionResult result;
    result.run.selection = ValidityCount{.valid = admitted.load(std::memory_order_relaxed), .total = total};
    result.run.outputsWritten = result.run.selection.valid;
    result.channelCount = channels;
    return result;
}

} // namespace

Result<RangeSelectionResult> rangeSelectImage(const ExecutionContext& execution, SampleType sampleType,
                                              const void* sourceSamples, const ImageLayout& sourceLayout,
                                              void* destinationSamples, const ImageLayout& destinationLayout,
                                              const RangeSelectionRequest& request, const KernelValidityView& validity)
{
    if (!execution.valid())
    {
        return rangeFailure(ErrorCode::NotInitialized, "range selection was given an empty execution context");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (sampleType != SampleType::Float32 && sampleType != SampleType::Float64)
    {
        return rangeFailure(ErrorCode::InvalidArgument, "range selection requires a Float32 or Float64 working image");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (sourceSamples == nullptr || destinationSamples == nullptr || sourceLayout.elementSizeBytes() != elementBytes ||
        destinationLayout.elementSizeBytes() != elementBytes || !isSampleAligned(sourceSamples, elementBytes) ||
        !isSampleAligned(destinationSamples, elementBytes))
    {
        return rangeFailure(ErrorCode::InvalidArgument,
                            "range-selection pointers and layouts must describe aligned samples of the stated type");
    }
    if (sourceLayout.extent() != destinationLayout.extent())
    {
        return rangeFailure(ErrorCode::IncompatibleArguments,
                            "range-selection source and destination must describe the same image extent");
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(sourceSamples), sourceLayout.totalBytes(),
                       static_cast<const std::uint8_t*>(destinationSamples), destinationLayout.totalBytes()) &&
        !exactlyAliases(sourceSamples, sourceLayout, destinationSamples, destinationLayout))
    {
        return rangeFailure(ErrorCode::IncompatibleArguments,
                            "range-selection source and destination partially overlap");
    }
    const Status valid = validateValidity(validity, sourceLayout, destinationSamples, destinationLayout);
    if (valid.failed())
    {
        return valid;
    }
    if (!std::isfinite(request.lowerLimit) || !std::isfinite(request.upperLimit) || !std::isfinite(request.fuzziness))
    {
        return rangeFailure(ErrorCode::InvalidArgument, "range-selection parameters must be finite");
    }
    if (request.lowerLimit < 0.0 || request.upperLimit > 1.0 || request.upperLimit < request.lowerLimit ||
        request.fuzziness < 0.0 || request.fuzziness > 1.0)
    {
        return rangeFailure(ErrorCode::InvalidArgument,
                            "range-selection limits and fuzziness must form normalized closed intervals");
    }

    if (sampleType == SampleType::Float32)
    {
        return selectTyped<float>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout,
                                  request, validity);
    }
    return selectTyped<double>(execution, sourceSamples, sourceLayout, destinationSamples, destinationLayout, request,
                               validity);
}

} // namespace an

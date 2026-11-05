// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "algorithms/demosaic/DemosaicInternal.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// The shared plumbing of the demosaic domain.
//
// The only judgement in this file is which images can be reached where they lie. Everything else is
// arithmetic-free: a widening pass, a narrowing pass under a stated conversion policy, and the
// argument checks the four entry points share. Keeping the judgement in one function is what makes
// "a Float32 planar frame costs no copy and everything else costs one, and the result says which"
// a property rather than four separate claims.

namespace an::demosaic_detail
{

Status demosaicFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

Status demosaicLimitFailure(ErrorCode code, const char* operation, const char* message, std::uint64_t requested,
                            std::uint64_t limit)
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = operation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                          StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

TaskCost transferRowCost(std::uint32_t width) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * 8u, .operationsPerItem = samples * 4u};
}

std::uint64_t channelRowByteOffset(const ImageLayout& layout, std::uint32_t channelIndex, std::uint64_t y) noexcept
{
    const std::uint32_t plane = channelIndex / layout.samplesPerPixel();
    const std::uint32_t within = channelIndex % layout.samplesPerPixel();
    return static_cast<std::uint64_t>(plane) * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
           static_cast<std::uint64_t>(within) * static_cast<std::uint64_t>(layout.elementSizeBytes());
}

bool rangesOverlap(const void* first, std::uint64_t firstBytes, const void* second, std::uint64_t secondBytes) noexcept
{
    if (firstBytes == 0u || secondBytes == 0u)
    {
        return false;
    }
    const std::uintptr_t left = reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t right = reinterpret_cast<std::uintptr_t>(second);
    constexpr std::uintptr_t kMaximum = (std::numeric_limits<std::uintptr_t>::max)();
    if (firstBytes > kMaximum - left || secondBytes > kMaximum - right)
    {
        return true;
    }
    const std::uintptr_t leftEnd = left + static_cast<std::uintptr_t>(firstBytes);
    const std::uintptr_t rightEnd = right + static_cast<std::uintptr_t>(secondBytes);
    return left < rightEnd && right < leftEnd;
}

bool channelIsAddressablePlane(SampleType sampleType, const ImageLayout& layout, const void* samples) noexcept
{
    if (sampleType != SampleType::Float32 || layout.samplesPerPixel() != 1u)
    {
        return false;
    }
    if (layout.rowStrideBytes() % sizeof(float) != 0u || layout.planeStrideBytes() % sizeof(float) != 0u)
    {
        return false;
    }
    return isAligned(reinterpret_cast<std::uint64_t>(samples), alignof(float));
}

FloatPlane borrowFloatPlane(void* samples, const ImageLayout& layout, std::uint32_t channelIndex) noexcept
{
    if (channelIndex >= layout.extent().channelCount())
    {
        return FloatPlane{};
    }
    auto* const bytes =
        static_cast<std::uint8_t*>(samples) + static_cast<std::uint64_t>(channelIndex) * layout.planeStrideBytes();
    return FloatPlane{.data = reinterpret_cast<float*>(bytes), .stride = layout.rowStrideBytes() / sizeof(float)};
}

Result<WorkingPlane> WorkingPlane::take(const ExecutionContext& execution, std::uint32_t width, std::uint32_t height,
                                        const char* operation)
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    Result<plane_detail::ScopedContextMemory> memory =
        plane_detail::ScopedContextMemory::take(execution, samples * sizeof(float), kCacheLineBytes, operation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    plane_detail::ScopedContextMemory taken = std::move(memory).value();
    const FloatPlane plane{.data = static_cast<float*>(taken.data()), .stride = width};
    return WorkingPlane{std::move(taken), plane};
}

Status gatherPlane(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                   const ImageLayout& layout, std::uint32_t channelIndex, FloatPlane destination, std::uint32_t width,
                   std::uint32_t height, const char* operation)
{
    if (channelIndex >= layout.extent().channelCount())
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation, "the image does not have that channel");
    }
    const std::uint32_t columnStride = layout.samplesPerPixel();
    Status dispatched = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        dispatched = parallelFor(execution, height, transferRowCost(width), [&](TaskRange range) {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const auto* const sourceRow = reinterpret_cast<const SampleT*>(
                    static_cast<const std::uint8_t*>(samples) + channelRowByteOffset(layout, channelIndex, y));
                float* const destinationRow = destination.data + y * destination.stride;
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    destinationRow[x] = static_cast<float>(sourceRow[static_cast<std::size_t>(x) * columnStride]);
                }
            }
            return Status::success();
        });
    });
    if (!known)
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation,
                               "the representation is not one of the eight this engine stores");
    }
    return dispatched;
}

namespace
{

/// Whether every sample of a row is finite, read from their exponent bits.
///
/// @param row the samples.
/// @param width how many.
/// @return true when none is infinite or not a number.
[[nodiscard]] bool rowIsFinite(const float* row, std::uint32_t width) noexcept
{
    std::uint32_t nonFinite = 0u;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        nonFinite |= (std::bit_cast<std::uint32_t>(row[x]) & 0x7f800000u) == 0x7f800000u ? 1u : 0u;
    }
    return nonFinite == 0u;
}

} // namespace

Status scatterPlane(const ExecutionContext& execution, FloatPlane source, SampleType sampleType, void* samples,
                    const ImageLayout& layout, std::uint32_t channelIndex, std::uint32_t width, std::uint32_t height,
                    const ConversionPolicy& conversion, ConversionCounters& counters, const char* operation)
{
    if (channelIndex >= layout.extent().channelCount())
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation, "the image does not have that channel");
    }
    const std::uint32_t columnStride = layout.samplesPerPixel();
    std::atomic<std::uint64_t> convertedCount{0u};
    std::atomic<std::uint64_t> nonFiniteCount{0u};
    std::atomic<std::uint64_t> saturatedCount{0u};
    Status dispatched = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        dispatched = parallelFor(execution, height, transferRowCost(width), [&](TaskRange range) {
            ConversionCounters local;
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                auto* const destinationRow = reinterpret_cast<SampleT*>(static_cast<std::uint8_t*>(samples) +
                                                                        channelRowByteOffset(layout, channelIndex, y));
                const float* const sourceRow = source.data + y * source.stride;
                if constexpr (std::is_same_v<SampleT, float>)
                {
                    // A Float32 sample converts to itself whatever the policy, so a row with nothing to
                    // count or refuse is copied - or left where it is, when the estimate wrote it there.
                    if (rowIsFinite(sourceRow, width))
                    {
                        if (columnStride == 1u)
                        {
                            if (destinationRow != sourceRow)
                            {
                                std::memcpy(destinationRow, sourceRow, static_cast<std::size_t>(width) * sizeof(float));
                            }
                        }
                        else
                        {
                            for (std::uint32_t x = 0u; x < width; ++x)
                            {
                                destinationRow[static_cast<std::size_t>(x) * columnStride] = sourceRow[x];
                            }
                        }
                        local.samplesConverted += width;
                        continue;
                    }
                }
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const ConvertedSample<SampleT> converted = convertSample<float, SampleT>(sourceRow[x], conversion);
                    if (converted.sourceWasNonFinite)
                    {
                        ++local.nonFiniteInputs;
                        if (conversion.nonFinite == NonFinitePolicy::Reject)
                        {
                            convertedCount.fetch_add(local.samplesConverted, std::memory_order_relaxed);
                            nonFiniteCount.fetch_add(local.nonFiniteInputs, std::memory_order_relaxed);
                            saturatedCount.fetch_add(local.saturatedSamples, std::memory_order_relaxed);
                            return Status::failure(
                                ErrorCode::InvalidData,
                                StatusDetail{
                                    .message = "a demosaic output is not finite and write back refuses one",
                                    .operation = operation,
                                    .domain = ErrorDomain::Algorithms,
                                    .context = {StatusContextField::ofCount(ContextKey::Completed,
                                                                            y * static_cast<std::uint64_t>(width) + x),
                                                StatusContextField::ofCount(
                                                    ContextKey::Total, static_cast<std::uint64_t>(width) * height)}});
                        }
                    }
                    if (converted.saturated)
                    {
                        ++local.saturatedSamples;
                        if (conversion.range == RangePolicy::Reject)
                        {
                            convertedCount.fetch_add(local.samplesConverted, std::memory_order_relaxed);
                            nonFiniteCount.fetch_add(local.nonFiniteInputs, std::memory_order_relaxed);
                            saturatedCount.fetch_add(local.saturatedSamples, std::memory_order_relaxed);
                            return Status::failure(
                                ErrorCode::OutOfRange,
                                StatusDetail{
                                    .message = "a demosaic output is outside the destination representation and "
                                               "write back refuses one",
                                    .operation = operation,
                                    .domain = ErrorDomain::Algorithms,
                                    .context = {StatusContextField::ofCount(ContextKey::Completed,
                                                                            y * static_cast<std::uint64_t>(width) + x),
                                                StatusContextField::ofCount(
                                                    ContextKey::Total, static_cast<std::uint64_t>(width) * height)}});
                        }
                    }
                    destinationRow[static_cast<std::size_t>(x) * columnStride] = converted.value;
                    ++local.samplesConverted;
                }
            }
            convertedCount.fetch_add(local.samplesConverted, std::memory_order_relaxed);
            nonFiniteCount.fetch_add(local.nonFiniteInputs, std::memory_order_relaxed);
            saturatedCount.fetch_add(local.saturatedSamples, std::memory_order_relaxed);
            return Status::success();
        });
    });
    if (!known)
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation,
                               "the representation is not one of the eight this engine stores");
    }
    counters.samplesConverted += convertedCount.load(std::memory_order_relaxed);
    counters.nonFiniteInputs += nonFiniteCount.load(std::memory_order_relaxed);
    counters.saturatedSamples += saturatedCount.load(std::memory_order_relaxed);
    return dispatched;
}

Status checkSelectionMask(const ValidityDescriptor& validity, const ValidityMaskView<const std::uint8_t>* mask,
                          std::uint32_t width, std::uint32_t height, std::uint32_t firstPlaneCount,
                          std::uint32_t secondPlaneCount, const char* operation)
{
    if (!validity.needsMask())
    {
        return mask == nullptr
                   ? Status::success()
                   : demosaicFailure(ErrorCode::InvalidArgument, operation,
                                     "a packed mask was supplied for a validity rule that does not use one");
    }
    if (mask == nullptr)
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation,
                               "the explicit-mask validity rule was given no packed mask");
    }
    const bool acceptedPlanes =
        mask->planeCount() == firstPlaneCount || (secondPlaneCount != 0u && mask->planeCount() == secondPlaneCount);
    if (mask->width() != width || mask->height() != height || !acceptedPlanes)
    {
        return demosaicFailure(ErrorCode::IncompatibleArguments, operation,
                               "the packed mask does not have the source extent and accepted plane count");
    }
    return Status::success();
}

Status checkProducedMask(const ValidityMaskView<std::uint8_t>* mask, std::uint32_t width, std::uint32_t height,
                         std::uint32_t firstPlaneCount, std::uint32_t secondPlaneCount, const char* operation)
{
    if (mask == nullptr)
    {
        return Status::success();
    }
    const bool acceptedPlanes =
        mask->planeCount() == firstPlaneCount || (secondPlaneCount != 0u && mask->planeCount() == secondPlaneCount);
    if (mask->width() != width || mask->height() != height || !acceptedPlanes)
    {
        return demosaicFailure(ErrorCode::IncompatibleArguments, operation,
                               "the produced mask does not have the destination extent and accepted plane count");
    }
    return Status::success();
}

Status checkMaskAliasing(const void* source, std::uint64_t sourceBytes, const void* destination,
                         std::uint64_t destinationBytes, const ValidityMaskView<const std::uint8_t>* sourceMask,
                         const ValidityMaskView<std::uint8_t>* producedMask, const char* operation)
{
    if (sourceMask != nullptr &&
        rangesOverlap(sourceMask->data(), sourceMask->spannedByteCount(), destination, destinationBytes))
    {
        return demosaicFailure(ErrorCode::Aliasing, operation,
                               "the destination sample buffer overlaps the source validity mask");
    }
    if (producedMask == nullptr)
    {
        return Status::success();
    }
    const void* const produced = producedMask->data();
    const std::uint64_t producedBytes = producedMask->spannedByteCount();
    if (rangesOverlap(produced, producedBytes, source, sourceBytes) ||
        rangesOverlap(produced, producedBytes, destination, destinationBytes) ||
        (sourceMask != nullptr &&
         rangesOverlap(produced, producedBytes, sourceMask->data(), sourceMask->spannedByteCount())))
    {
        return demosaicFailure(ErrorCode::Aliasing, operation,
                               "the produced validity mask overlaps another buffer used by the operation");
    }
    return Status::success();
}

Status checkImagePair(const ExecutionContext& execution, SampleType sampleType, const void* source,
                      const ImageLayout& sourceLayout, std::uint32_t sourceChannels, const void* destination,
                      const ImageLayout& destinationLayout, std::uint32_t destinationChannels, const char* operation)
{
    if (!execution.valid())
    {
        return demosaicFailure(ErrorCode::NotInitialized, operation, "the execution context holds nothing");
    }
    const Status cancelled = execution.checkCancellation();
    if (cancelled.failed())
    {
        return cancelled;
    }
    if (source == nullptr || destination == nullptr)
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation, "both images must be present");
    }
    if (!isSampleType(sampleType))
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation,
                               "the representation is not one of the eight this engine stores");
    }
    const std::uint32_t elementSize = sampleTypeSizeBytes(sampleType);
    if (sourceLayout.elementSizeBytes() != elementSize || destinationLayout.elementSizeBytes() != elementSize)
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation,
                               "a layout describes an element width the representation does not have");
    }
    if (reinterpret_cast<std::uintptr_t>(source) % elementSize != 0u ||
        reinterpret_cast<std::uintptr_t>(destination) % elementSize != 0u)
    {
        return demosaicFailure(ErrorCode::InvalidArgument, operation,
                               "an image is not aligned for its resident sample type");
    }
    if (sourceLayout.extent().channelCount() != sourceChannels)
    {
        return demosaicFailure(ErrorCode::IncompatibleArguments, operation,
                               "the source does not have the channel count this operation reads");
    }
    if (destinationLayout.extent().channelCount() != destinationChannels)
    {
        return demosaicFailure(ErrorCode::IncompatibleArguments, operation,
                               "the destination does not have the channel count this operation writes");
    }
    if (rangesOverlap(source, sourceLayout.totalBytes(), destination, destinationLayout.totalBytes()))
    {
        return demosaicFailure(ErrorCode::Aliasing, operation,
                               "the destination shares memory with the source, so a neighbourhood read after a "
                               "partial write would depend on the order the tiles were scheduled in");
    }
    return Status::success();
}

} // namespace an::demosaic_detail

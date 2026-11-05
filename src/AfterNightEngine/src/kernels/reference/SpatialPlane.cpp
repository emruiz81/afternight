// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the checks, working planes, gather, scatter and plane passes shared by the
// neighbourhood filters.
//
// Every loop here walks rows of the picture through `parallelFor` on the operation's threads, with a
// cost stated per row so the grain policy divides a tall picture across the workers. Each row is
// written by exactly one worker and a count is summed through one relaxed atomic per chunk, so the
// planes and the counts are the same at every thread count. The scatter converts each answer with
// `convertSample` under the caller's policy and writes only where the filter's input sample meant
// something, which is the family's rule that a rejected centre is left as it was; the commit is the
// same rows written on the calling thread without a stop, for a caller that must not leave a
// destination half written.

#include "kernels/reference/SpatialPlane.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace an::spatial_detail
{
namespace
{

/// What one gathered, scattered, copied or blended row costs, for the grain policy.
///
/// The item of every loop in this file is a row of the picture rather than a sample of it, so the
/// cost has to be a row as well. Stating it per sample is the mistake that makes a thousand-row
/// image one chunk and a parallel loop single-threaded, which is a correctness-preserving way of
/// throwing away every core but one.
///
/// @param width how many pixels one row holds.
/// @return the bytes one row touches and roughly what it costs to move them.
[[nodiscard]] TaskCost transferRowCost(std::uint32_t width) noexcept
{
    const std::uint64_t samples = static_cast<std::uint64_t>(width);
    return TaskCost{.bytesPerItem = samples * 8u, .operationsPerItem = samples * 4u};
}

/// A Float32 plane over a buffer of a stated geometry.
///
/// The span a `PlaneView` is built from has to hold the padding between rows but not any after the
/// last one, and getting that arithmetic wrong once is what a shared helper exists to prevent.
///
/// @param base the first byte of the plane, aligned for Float32.
/// @param width the plane's width in pixels.
/// @param height the plane's height in pixels.
/// @param rowStrideBytes distance in bytes from one row to the next.
/// @return the view, or no value when the geometry does not describe a plane.
[[nodiscard]] std::optional<PlaneView<float>> planeOver(void* base, std::uint32_t width, std::uint32_t height,
                                                        std::uint64_t rowStrideBytes) noexcept
{
    if (base == nullptr || width == 0u || height == 0u || rowStrideBytes % sizeof(float) != 0u)
    {
        return std::nullopt;
    }
    const std::uint64_t rowSamples = rowStrideBytes / sizeof(float);
    if (rowSamples < width)
    {
        return std::nullopt;
    }
    const std::uint64_t spannedSamples = (static_cast<std::uint64_t>(height) - 1u) * rowSamples + width;
    const std::span<float> samples{static_cast<float*>(base), static_cast<std::size_t>(spannedSamples)};
    return PlaneView<float>::create(samples, width, height, 1u, rowStrideBytes);
}

/// Whether two byte ranges share a byte.
///
/// @param first where one range starts.
/// @param firstBytes how long it is.
/// @param second where the other starts.
/// @param secondBytes how long it is.
/// @return true when the two ranges intersect.
[[nodiscard]] bool rangesOverlap(const void* first, std::uint64_t firstBytes, const void* second,
                                 std::uint64_t secondBytes) noexcept
{
    const auto* const left = static_cast<const std::uint8_t*>(first);
    const auto* const right = static_cast<const std::uint8_t*>(second);
    return left < right + secondBytes && right < left + firstBytes;
}

/// Writes a contiguous range of rows for one instantiated destination representation.
///
/// @tparam SampleT the destination sample type.
/// @param filtered the computed answers.
/// @param filterInput the samples the validity rule describes.
/// @param validity the value rule and optional mask.
/// @param samples the destination storage.
/// @param layout the destination layout.
/// @param channelIndex the destination channel.
/// @param conversion how an answer becomes a stored sample.
/// @param originalSamples optional source storage used to count changes.
/// @param originalLayout the source layout when changes are counted.
/// @param firstRow the first row to write.
/// @param lastRow one past the last row to write.
/// @return how many stored samples differ from their originals.
template <typename SampleT>
[[nodiscard]] std::uint64_t
writeChannelRows(PlaneView<const float> filtered, PlaneView<const float> filterInput,
                 const KernelValidityView& validity, void* samples, const ImageLayout& layout,
                 std::uint32_t channelIndex, const ConversionPolicy& conversion, const void* originalSamples,
                 const ImageLayout* originalLayout, std::uint64_t firstRow, std::uint64_t lastRow)
{
    const std::uint32_t width = filtered.width();
    const std::uint32_t columnStride = layout.samplesPerPixel();
    const bool everySampleMeansSomething =
        validity.mask == nullptr && validity.descriptor.rule() == ValidityRule::AllValid;
    std::uint64_t changed = 0u;
    for (std::uint64_t y = firstRow; y < lastRow; ++y)
    {
        auto* const destinationRow = reinterpret_cast<SampleT*>(
            static_cast<std::uint8_t*>(samples) + plane_detail::channelRowByteOffset(layout, channelIndex, y));
        const std::span<const float> answer = *filtered.row(static_cast<std::uint32_t>(y));
        const std::span<const float> input = *filterInput.row(static_cast<std::uint32_t>(y));
        const auto* const originalRow =
            originalSamples != nullptr
                ? reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(originalSamples) +
                                                   plane_detail::channelRowByteOffset(*originalLayout, channelIndex, y))
                : nullptr;
        const std::uint32_t originalColumnStride = originalLayout != nullptr ? originalLayout->samplesPerPixel() : 0u;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            if (!everySampleMeansSomething && (!sampleIsValidByValue(validity.descriptor, input[x]) ||
                                               !plane_detail::maskAllows(validity, x, static_cast<std::uint32_t>(y))))
            {
                continue;
            }
            const SampleT stored = convertSample<float, SampleT>(answer[x], conversion).value;
            destinationRow[static_cast<std::size_t>(x) * columnStride] = stored;
            if (originalRow != nullptr && stored != originalRow[static_cast<std::size_t>(x) * originalColumnStride])
            {
                ++changed;
            }
        }
    }
    return changed;
}

} // namespace

Status spatialFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

Status spatialLimitFailure(ErrorCode code, const char* operation, const char* message, std::uint64_t requested,
                           std::uint64_t limit)
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = operation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                                          StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

Status checkSpatialImage(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                         const ImageLayout& layout, const SpatialOutputPolicy& output,
                         const SpatialSelection& selection, const char* operation, SpatialGeometry& geometry)
{
    if (!execution.valid())
    {
        return spatialFailure(ErrorCode::NotInitialized, operation,
                              "a neighbourhood filter needs an execution holding an engine-context lease");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    if (!isSampleType(sampleType))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the representation is not one of the eight this engine stores");
    }
    if (samples == nullptr)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation, "a filter needs an image to read or write");
    }
    if (layout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a layout's element size is not the stated representation's");
    }

    if (!isOutputPrecisionContract(output.contract))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the output contract is not one this engine knows");
    }
    const PrecisionError precision = spatialPrecisionError(sampleType, output.contract);
    if (precision != PrecisionError::None)
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, operation,
                              "this family computes in Float32, which cannot keep the promise this storage and "
                              "contract make; state a normalized or lossy contract, or convert the image first");
    }
    const Status policy = checkConversionPolicy(output.writeBack, operation);
    if (policy.failed())
    {
        return policy;
    }
    if (output.writeBack.domain != DomainMapping::CodeValue)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a neighbourhood filter carries its samples across as the numbers they are; a "
                              "full-scale domain mapping would make it a filter and a domain change at once");
    }

    if (selection.validity.rule() == ValidityRule::ExplicitMask)
    {
        if (selection.mask == nullptr)
        {
            return spatialFailure(ErrorCode::InvalidArgument, operation,
                                  "an explicit-mask rule needs the packed bits that decide");
        }
    }
    else if (selection.mask != nullptr)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a packed mask was supplied for a rule that does not read one");
    }

    const ImageExtent& extent = layout.extent();
    if (selection.mask != nullptr)
    {
        const Status covers = checkMaskCovers(*selection.mask, extent, operation);
        if (covers.failed())
        {
            return covers;
        }
    }
    geometry.width = extent.width();
    geometry.height = extent.height();
    geometry.channelCount = extent.channelCount();
    geometry.planeBytes = static_cast<std::uint64_t>(geometry.width) * static_cast<std::uint64_t>(geometry.height) *
                          static_cast<std::uint64_t>(sizeof(float));
    return Status::success();
}

Status checkSpatialImages(const ExecutionContext& execution, SampleType sampleType, const void* source,
                          const ImageLayout& sourceLayout, const void* destination,
                          const ImageLayout& destinationLayout, const SpatialOutputPolicy& output,
                          const SpatialSelection& selection, const char* operation, SpatialGeometry& geometry)
{
    const Status readable =
        checkSpatialImage(execution, sampleType, source, sourceLayout, output, selection, operation, geometry);
    if (readable.failed())
    {
        return readable;
    }
    if (destination == nullptr)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation, "a filter needs both a source and a destination");
    }
    if (destinationLayout.elementSizeBytes() != sampleTypeSizeBytes(sampleType))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "a layout's element size is not the stated representation's");
    }
    const ImageExtent& sourceExtent = sourceLayout.extent();
    const ImageExtent& destinationExtent = destinationLayout.extent();
    if (sourceExtent.width() != destinationExtent.width() || sourceExtent.height() != destinationExtent.height() ||
        sourceExtent.channelCount() != destinationExtent.channelCount())
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, operation,
                              "the source and the destination are not the same picture");
    }
    if (rangesOverlap(source, sourceLayout.totalBytes(), destination, destinationLayout.totalBytes()))
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, operation,
                              "the destination shares memory with the source, so a neighbourhood read after a "
                              "partial write would depend on the order the rows were scheduled in");
    }
    return Status::success();
}

Status substituteRejectedSamples(const ExecutionContext& execution, PlaneView<float> plane,
                                 const KernelValidityView& validity, float substitute, std::uint64_t& substituted,
                                 const char* operation)
{
    (void)operation;
    if (validity.mask == nullptr && validity.descriptor.rule() == ValidityRule::AllValid)
    {
        return Status::success();
    }
    const std::uint32_t width = plane.width();
    std::atomic<std::uint64_t> replaced{0u};
    const Status walked = parallelFor(execution, plane.height(), transferRowCost(width), [&](TaskRange range) {
        std::uint64_t local = 0u;
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::span<float> row = *plane.row(static_cast<std::uint32_t>(y));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (sampleIsValidByValue(validity.descriptor, row[x]) &&
                    plane_detail::maskAllows(validity, x, static_cast<std::uint32_t>(y)))
                {
                    continue;
                }
                row[x] = substitute;
                ++local;
            }
        }
        replaced.fetch_add(local, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.failed())
    {
        return walked;
    }
    substituted += replaced.load(std::memory_order_relaxed);
    return Status::success();
}

Result<SpatialPlaneBuffer> SpatialPlaneBuffer::own(const ExecutionContext& execution, const SpatialGeometry& geometry,
                                                   const char* operation)
{
    Result<plane_detail::ScopedContextMemory> memory = plane_detail::ScopedContextMemory::take(
        execution, geometry.planeBytes, kDefaultBufferAlignmentBytes, operation);
    if (!memory.hasValue())
    {
        return memory.status();
    }
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(geometry.width) * sizeof(float);
    const std::optional<PlaneView<float>> view =
        planeOver(memory.value().data(), geometry.width, geometry.height, rowBytes);
    if (!view.has_value())
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the picture is too large for one working plane to describe");
    }
    return SpatialPlaneBuffer{std::move(memory).value(), *view};
}

bool channelIsAddressablePlane(SampleType sampleType, const ImageLayout& layout) noexcept
{
    return sampleType == SampleType::Float32 && layout.samplesPerPixel() == 1u;
}

std::optional<PlaneView<float>> borrowDestinationPlane(void* samples, const ImageLayout& layout,
                                                       std::uint32_t channelIndex, SampleType sampleType,
                                                       const SpatialGeometry& geometry) noexcept
{
    if (!channelIsAddressablePlane(sampleType, layout) || channelIndex >= layout.planeCount())
    {
        return std::nullopt;
    }
    auto* const base =
        static_cast<std::uint8_t*>(samples) + static_cast<std::uint64_t>(channelIndex) * layout.planeStrideBytes();
    return planeOver(base, geometry.width, geometry.height, layout.rowStrideBytes());
}

std::optional<PlaneView<const float>> borrowSourcePlane(const void* samples, const ImageLayout& layout,
                                                        std::uint32_t channelIndex, SampleType sampleType,
                                                        const SpatialGeometry& geometry) noexcept
{
    // The plane is only ever read through the returned read-only view, which is what makes reaching
    // a const image through the writable builder safe rather than merely convenient.
    const std::optional<PlaneView<float>> writable =
        borrowDestinationPlane(const_cast<void*>(samples), layout, channelIndex, sampleType, geometry);
    if (!writable.has_value())
    {
        return std::nullopt;
    }
    return PlaneView<const float>{*writable};
}

Status gatherChannelPlane(const ExecutionContext& execution, SampleType sampleType, const void* samples,
                          const ImageLayout& layout, std::uint32_t channelIndex, PlaneView<float> destination,
                          const char* operation)
{
    if (channelIndex >= layout.extent().channelCount())
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation, "the image does not have that channel");
    }
    const std::uint32_t width = destination.width();
    const std::uint32_t columnStride = layout.samplesPerPixel();
    Status dispatched = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        dispatched = parallelFor(execution, destination.height(), transferRowCost(width), [&](TaskRange range) {
            for (std::uint64_t y = range.begin; y < range.end; ++y)
            {
                const auto* const sourceRow =
                    reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(samples) +
                                                     plane_detail::channelRowByteOffset(layout, channelIndex, y));
                const std::span<float> destinationRow = *destination.row(static_cast<std::uint32_t>(y));
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
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the representation is not one of the eight this engine stores");
    }
    return dispatched;
}

Status scatterChannelPlane(const ExecutionContext& execution, PlaneView<const float> filtered,
                           PlaneView<const float> filterInput, const KernelValidityView& validity,
                           SampleType sampleType, void* samples, const ImageLayout& layout, std::uint32_t channelIndex,
                           const ConversionPolicy& conversion, const char* operation, const void* originalSamples,
                           const ImageLayout* originalLayout, std::uint64_t* samplesChanged)
{
    if (channelIndex >= layout.extent().channelCount())
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation, "the image does not have that channel");
    }
    if (samplesChanged != nullptr && (originalSamples == nullptr || originalLayout == nullptr))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "counting stored changes needs the source storage and its layout");
    }
    Status dispatched = Status::success();
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        std::atomic<std::uint64_t> changed{0u};
        dispatched = parallelFor(execution, filtered.height(), transferRowCost(filtered.width()), [&](TaskRange range) {
            const std::uint64_t localChanged =
                writeChannelRows<SampleT>(filtered, filterInput, validity, samples, layout, channelIndex, conversion,
                                          samplesChanged != nullptr ? originalSamples : nullptr,
                                          samplesChanged != nullptr ? originalLayout : nullptr, range.begin, range.end);
            changed.fetch_add(localChanged, std::memory_order_relaxed);
            return Status::success();
        });
        if (dispatched.ok() && samplesChanged != nullptr)
        {
            *samplesChanged += changed.load(std::memory_order_relaxed);
        }
    });
    if (!known)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the representation is not one of the eight this engine stores");
    }
    return dispatched;
}

Status commitChannelPlane(PlaneView<const float> filtered, PlaneView<const float> filterInput,
                          const KernelValidityView& validity, SampleType sampleType, void* samples,
                          const ImageLayout& layout, std::uint32_t channelIndex, const ConversionPolicy& conversion,
                          const char* operation, const void* originalSamples, const ImageLayout* originalLayout,
                          std::uint64_t* samplesChanged)
{
    if (channelIndex >= layout.extent().channelCount())
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation, "the image does not have that channel");
    }
    if (samplesChanged != nullptr && (originalSamples == nullptr || originalLayout == nullptr))
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "counting stored changes needs the source storage and its layout");
    }
    std::uint64_t changed = 0u;
    const bool known = dispatchSampleType(sampleType, [&]<typename SampleT>() {
        changed =
            writeChannelRows<SampleT>(filtered, filterInput, validity, samples, layout, channelIndex, conversion,
                                      samplesChanged != nullptr ? originalSamples : nullptr,
                                      samplesChanged != nullptr ? originalLayout : nullptr, 0u, filtered.height());
    });
    if (!known)
    {
        return spatialFailure(ErrorCode::InvalidArgument, operation,
                              "the representation is not one of the eight this engine stores");
    }
    if (samplesChanged != nullptr)
    {
        *samplesChanged += changed;
    }
    return Status::success();
}

Status countChangedPlane(const ExecutionContext& execution, PlaneView<const float> filtered,
                         PlaneView<const float> filterInput, const KernelValidityView& validity, std::uint64_t& changed)
{
    const std::uint32_t width = filtered.width();
    std::atomic<std::uint64_t> counted{0u};
    const Status walked = parallelFor(execution, filtered.height(), transferRowCost(width), [&](TaskRange range) {
        std::uint64_t local = 0u;
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::span<const float> answer = *filtered.row(static_cast<std::uint32_t>(y));
            const std::span<const float> input = *filterInput.row(static_cast<std::uint32_t>(y));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (sampleIsValidByValue(validity.descriptor, input[x]) &&
                    plane_detail::maskAllows(validity, x, static_cast<std::uint32_t>(y)) && answer[x] != input[x])
                {
                    ++local;
                }
            }
        }
        counted.fetch_add(local, std::memory_order_relaxed);
        return Status::success();
    });
    if (walked.ok())
    {
        changed += counted.load(std::memory_order_relaxed);
    }
    return walked;
}

Status copyPlane(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                 const char* operation)
{
    if (source.width() != destination.width() || source.height() != destination.height())
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, operation, "two working planes are not one picture");
    }
    const std::uint32_t width = source.width();
    return parallelFor(execution, source.height(), transferRowCost(width), [&](TaskRange range) {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::span<const float> from = *source.row(static_cast<std::uint32_t>(y));
            const std::span<float> to = *destination.row(static_cast<std::uint32_t>(y));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                to[x] = from[x];
            }
        }
        return Status::success();
    });
}

KernelValidityView channelValidity(const SpatialSelection& selection, std::uint32_t channelIndex) noexcept
{
    KernelValidityView validity{.descriptor = selection.validity, .mask = selection.mask, .maskPlane = 0u};
    if (selection.mask != nullptr && selection.mask->planeCount() > 1u)
    {
        validity.maskPlane = channelIndex;
    }
    return validity;
}

Status blendPlane(const ExecutionContext& execution, PlaneView<const float> input, PlaneView<float> filtered,
                  const KernelValidityView& validity, double amount, const char* operation)
{
    if (input.width() != filtered.width() || input.height() != filtered.height())
    {
        return spatialFailure(ErrorCode::IncompatibleArguments, operation, "two working planes are not one picture");
    }
    const std::uint32_t width = input.width();
    const bool everySampleMeansSomething =
        validity.mask == nullptr && validity.descriptor.rule() == ValidityRule::AllValid;
    return parallelFor(execution, input.height(), transferRowCost(width), [&](TaskRange range) {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const std::span<const float> original = *input.row(static_cast<std::uint32_t>(y));
            const std::span<float> answer = *filtered.row(static_cast<std::uint32_t>(y));
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (!everySampleMeansSomething &&
                    (!sampleIsValidByValue(validity.descriptor, original[x]) ||
                     !plane_detail::maskAllows(validity, x, static_cast<std::uint32_t>(y))))
                {
                    continue;
                }
                const double blended =
                    amount * static_cast<double>(answer[x]) + (1.0 - amount) * static_cast<double>(original[x]);
                answer[x] = static_cast<float>(blended);
            }
        }
        return Status::success();
    });
}

} // namespace an::spatial_detail

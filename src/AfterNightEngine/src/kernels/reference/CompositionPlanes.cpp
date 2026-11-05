// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - planar working images for the compositions that read images of any representation.
//
// Reading walks a source a row at a time on the operation's threads, resolving its representation once per row
// through the published sample dispatch; writing does the same for the destination. A pixel's flag is cleared by
// any of its channels, and a row is only ever touched by the range that owns it, so neither walk shares a byte
// between threads.

#include "kernels/reference/CompositionPlanes.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/PlaneSourceAccess.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace an::composition_detail
{
namespace
{

[[nodiscard]] Status compositionFailure(ErrorCode code, const char* message, const char* operation,
                                        const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] bool alignedFor(const void* samples, std::uint32_t elementBytes) noexcept
{
    return elementBytes != 0u && reinterpret_cast<std::uintptr_t>(samples) % elementBytes == 0u;
}

[[nodiscard]] bool maskBit(const ValidityMaskView<const std::uint8_t>& mask, std::uint32_t plane, std::uint32_t x,
                           std::uint32_t y) noexcept
{
    const std::uint64_t offset = static_cast<std::uint64_t>(plane) * mask.planeStrideBytes() +
                                 static_cast<std::uint64_t>(y) * mask.rowStrideBytes() + x / 8u;
    return (mask.data()[offset] & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
}

/// Whether a source's mask gives each of its channels a plane of its own.
[[nodiscard]] bool perChannelMask(const CompositionSource& source) noexcept
{
    const std::uint32_t channels = source.descriptor->extent().channelCount();
    return source.validity.mask != nullptr && source.validity.mask->planeCount() == channels && channels > 1u;
}

/// One row of one channel of a source into a working row, as fractions of full scale.
template <typename SampleT, typename WorkingT>
[[nodiscard]] bool readRow(const CompositionSource& source, std::uint32_t channel, std::uint32_t y, WorkingT* out,
                           std::uint8_t* valid, bool ruled, bool ownPlanes) noexcept
{
    const ImageLayout& layout = *source.layout;
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t stride = layout.samplesPerPixel();
    const SampleT* const samples = reinterpret_cast<const SampleT*>(
        static_cast<const std::uint8_t*>(source.samples) + plane_detail::channelRowByteOffset(layout, channel, y));
    const KernelValidityView& validity = source.validity;
    const std::uint32_t maskPlane = ownPlanes ? channel : validity.maskPlane;
    const ConversionPolicy policy{.domain = DomainMapping::FullScale};
    bool finite = true;
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        const SampleT raw = samples[static_cast<std::size_t>(x) * stride];
        double value = 0.0;
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            value = static_cast<double>(raw);
        }
        else
        {
            value = convertSample<SampleT, double>(raw, policy).value;
        }
        if (ruled && !(sampleIsValidByValue(validity.descriptor, raw) &&
                       (validity.mask == nullptr || maskBit(*validity.mask, maskPlane, x, y))))
        {
            valid[x] = 0u;
            out[x] = WorkingT{0};
            continue;
        }
        if (!std::isfinite(value))
        {
            finite = false;
            out[x] = WorkingT{0};
            continue;
        }
        out[x] = static_cast<WorkingT>(value);
    }
    return finite;
}

/// One row of a working plane into one channel of a destination, converted once.
template <typename WorkingT, typename SampleT>
void writeRow(const WorkingT* in, std::uint8_t* destination, const ImageLayout& layout, std::uint32_t channel,
              std::uint32_t y) noexcept
{
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t stride = layout.samplesPerPixel();
    SampleT* const samples =
        reinterpret_cast<SampleT*>(destination + plane_detail::channelRowByteOffset(layout, channel, y));
    if constexpr (std::is_same_v<WorkingT, SampleT>)
    {
        if (stride == 1u)
        {
            std::memcpy(samples, in, static_cast<std::size_t>(width) * sizeof(SampleT));
            return;
        }
    }
    const ConversionPolicy policy{.domain = DomainMapping::FullScale};
    for (std::uint32_t x = 0u; x < width; ++x)
    {
        samples[static_cast<std::size_t>(x) * stride] = convertSample<WorkingT, SampleT>(in[x], policy).value;
    }
}

} // namespace

WorkingBlock::~WorkingBlock()
{
    if (m_pointer != nullptr)
    {
        m_memory->deallocate(m_pointer, m_bytes, kDefaultBufferAlignmentBytes);
    }
}

Status WorkingBlock::take(const ExecutionContext& execution, std::uint64_t bytes, const char* operation)
{
    if (bytes == 0u)
    {
        return Status::success();
    }
    MemoryResource* memory = execution.memoryResource();
    if (memory == nullptr)
    {
        return compositionFailure(ErrorCode::NotInitialized, "the execution has no memory resource", operation,
                                  "execution");
    }
    Result<void*> allocation = memory->allocate(bytes, kDefaultBufferAlignmentBytes);
    if (!allocation.hasValue())
    {
        return allocation.status();
    }
    m_memory = memory;
    m_pointer = *allocation;
    m_bytes = bytes;
    return Status::success();
}

Status checkCompositionSource(const CompositionSource& source, const char* operation, const char* parameter)
{
    if (source.descriptor == nullptr || source.samples == nullptr || source.layout == nullptr)
    {
        return compositionFailure(ErrorCode::InvalidArgument, "a source has a null member", operation, parameter);
    }
    const ImageDescriptor& descriptor = *source.descriptor;
    const ImageLayout& layout = *source.layout;
    const std::uint32_t elementBytes = sampleTypeSizeBytes(descriptor.sampleType());
    if (!(layout.extent() == descriptor.extent()) || layout.elementSizeBytes() != elementBytes ||
        layout.planeCount() * layout.samplesPerPixel() != descriptor.extent().channelCount() ||
        !alignedFor(source.samples, elementBytes))
    {
        return compositionFailure(ErrorCode::InvalidArgument,
                                  "a source's layout or pointer does not describe its descriptor", operation,
                                  parameter);
    }
    const KernelValidityView& validity = source.validity;
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return compositionFailure(ErrorCode::InvalidArgument, "a source's validity view supplies an unused mask",
                                      operation, parameter);
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return compositionFailure(ErrorCode::InvalidArgument, "an explicit-mask source supplies no mask", operation,
                                  parameter);
    }
    if (validity.mask->width() != descriptor.extent().width() ||
        validity.mask->height() != descriptor.extent().height() ||
        (!perChannelMask(source) && validity.maskPlane >= validity.mask->planeCount()))
    {
        return compositionFailure(ErrorCode::IncompatibleArguments, "a source's validity mask does not cover it",
                                  operation, parameter);
    }
    return Status::success();
}

bool compositionSourceRuled(const CompositionSource& source) noexcept
{
    return source.validity.descriptor.rule() != ValidityRule::AllValid || source.validity.mask != nullptr;
}

Status checkCompositionDestination(SampleType sampleType, const void* destination, const ImageLayout& layout,
                                   const ImageExtent& extent, const char* operation)
{
    if (destination == nullptr)
    {
        return compositionFailure(ErrorCode::InvalidArgument, "the destination is null", operation, "destination");
    }
    if (!isSampleType(sampleType))
    {
        return compositionFailure(ErrorCode::InvalidArgument,
                                  "the destination representation is outside its "
                                  "enumeration",
                                  operation, "destination");
    }
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    if (layout.elementSizeBytes() != elementBytes || !alignedFor(destination, elementBytes))
    {
        return compositionFailure(ErrorCode::InvalidArgument,
                                  "the destination's layout or pointer does not describe its representation", operation,
                                  "destinationLayout");
    }
    if (!(layout.extent() == extent))
    {
        return compositionFailure(ErrorCode::IncompatibleArguments,
                                  "the destination is not the width, height and channel count the composition writes",
                                  operation, "destinationLayout");
    }
    return Status::success();
}

Status checkCompositionSeparate(const CompositionSource& source, const void* destination,
                                std::uint64_t destinationBytes, const char* operation, const char* parameter)
{
    const auto* const bytes = static_cast<const std::uint8_t*>(destination);
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(), bytes,
                       destinationBytes))
    {
        return compositionFailure(ErrorCode::IncompatibleArguments, "the destination overlaps a source", operation,
                                  parameter);
    }
    if (source.validity.mask != nullptr &&
        regionsOverlap(source.validity.mask->data(), source.validity.mask->spannedByteCount(), bytes, destinationBytes))
    {
        return compositionFailure(ErrorCode::InvalidArgument, "a validity mask overlaps the destination", operation,
                                  parameter);
    }
    return Status::success();
}

std::optional<ImageLayout> workingPlaneLayout(const WorkingPlanes& working, std::uint32_t planes)
{
    const ImageLayout& whole = *working.layout;
    const std::optional<ImageExtent> extent =
        ImageExtent::create(whole.extent().width(), whole.extent().height(), planes, defaultEngineLimits());
    if (!extent.has_value() || planes == 0u || planes > whole.planeCount())
    {
        return std::nullopt;
    }
    return ImageLayout::createWithStrides(ImageLayoutRequest{.extent = *extent,
                                                             .elementSizeBytes = whole.elementSizeBytes(),
                                                             .samplesPerPixel = 1u,
                                                             .planeCount = planes,
                                                             .rowAlignmentBytes = whole.elementSizeBytes()},
                                          whole.rowStrideBytes(), whole.planeStrideBytes(), defaultEngineLimits());
}

Result<WorkingPlanes> takeWorkingPlanes(const ExecutionContext& execution, WorkingBlock& block, std::uint32_t width,
                                        std::uint32_t height, std::uint32_t channels, SampleType sampleType,
                                        const char* operation)
{
    const std::uint32_t elementBytes = sampleTypeSizeBytes(sampleType);
    const std::optional<ImageExtent> extent = ImageExtent::create(width, height, channels, execution.limits());
    if (!extent.has_value())
    {
        return compositionFailure(ErrorCode::InvalidArgument, "the working image's extent is refused by the limits",
                                  operation, "extent");
    }
    WorkingPlanes working;
    working.sampleType = sampleType;
    working.layout = ImageLayout::create(ImageLayoutRequest{.extent = *extent,
                                                            .elementSizeBytes = elementBytes,
                                                            .samplesPerPixel = 1u,
                                                            .planeCount = channels,
                                                            .rowAlignmentBytes = elementBytes},
                                         execution.limits());
    if (!working.layout.has_value())
    {
        return compositionFailure(ErrorCode::InvalidArgument, "the working image cannot be laid out", operation,
                                  "extent");
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(channels) * working.layout->planeStrideBytes();
    const Status taken = block.take(execution, bytes, operation);
    if (taken.failed())
    {
        return taken;
    }
    working.samples = block.bytes();
    return working;
}

Status readWorking(const ExecutionContext& execution, const CompositionSource& source, const WorkingPlanes& working,
                   std::uint32_t firstPlane, std::uint8_t* valid, const char* operation)
{
    const ImageLayout& layout = *source.layout;
    const std::uint32_t width = layout.extent().width();
    const std::uint32_t channels = layout.extent().channelCount();
    const bool ruled = compositionSourceRuled(source) && valid != nullptr;
    const bool ownPlanes = perChannelMask(source);
    std::atomic<bool> finite{true};
    const TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(width) * channels *
                                        (layout.elementSizeBytes() + sampleTypeSizeBytes(working.sampleType)),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels};
    const Status ran = parallelFor(execution, layout.extent().height(), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto row = static_cast<std::uint32_t>(y);
            std::uint8_t* const rowValid = ruled ? valid + y * width : nullptr;
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                // A checked descriptor names one of the eight representations, so the dispatch always runs.
                (void)dispatchSampleType(source.descriptor->sampleType(), [&]<typename SampleT>() noexcept {
                    const bool rowFinite =
                        working.sampleType == SampleType::Float64
                            ? readRow<SampleT>(source, channel, row, working.row<double>(firstPlane + channel, row),
                                               rowValid, ruled, ownPlanes)
                            : readRow<SampleT>(source, channel, row, working.row<float>(firstPlane + channel, row),
                                               rowValid, ruled, ownPlanes);
                    if (!rowFinite)
                    {
                        finite.store(false, std::memory_order_relaxed);
                    }
                });
            }
        }
        return Status::success();
    });
    if (ran.failed())
    {
        return ran;
    }
    if (!finite.load(std::memory_order_relaxed))
    {
        return compositionFailure(ErrorCode::InvalidData,
                                  "a source holds a sample that is not finite in a pixel its validity admits",
                                  operation, "source");
    }
    return Status::success();
}

void copyWorkingPlane(const WorkingPlanes& working, std::uint32_t from, std::uint32_t to) noexcept
{
    copyWorkingPlane(working, from, working, to);
}

void copyWorkingPlane(const WorkingPlanes& source, std::uint32_t from, const WorkingPlanes& destination,
                      std::uint32_t to) noexcept
{
    const ImageLayout& in = *source.layout;
    const ImageLayout& out = *destination.layout;
    const std::size_t rowBytes =
        static_cast<std::size_t>(in.extent().width()) * static_cast<std::size_t>(in.elementSizeBytes());
    for (std::uint32_t y = 0u; y < in.extent().height(); ++y)
    {
        std::memcpy(destination.plane(to) + static_cast<std::uint64_t>(y) * out.rowStrideBytes(),
                    source.plane(from) + static_cast<std::uint64_t>(y) * in.rowStrideBytes(), rowBytes);
    }
}

Status writeWorking(const ExecutionContext& execution, const WorkingPlanes& working, SampleType sampleType,
                    void* destination, const ImageLayout& destinationLayout, const char* operation)
{
    (void)operation;
    const std::uint32_t width = destinationLayout.extent().width();
    const std::uint32_t channels = destinationLayout.extent().channelCount();
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(destination);
    const TaskCost cost{.bytesPerItem =
                            static_cast<std::uint64_t>(width) * channels *
                            (destinationLayout.elementSizeBytes() + sampleTypeSizeBytes(working.sampleType)),
                        .operationsPerItem = static_cast<std::uint64_t>(width) * channels};
    return parallelFor(execution, destinationLayout.extent().height(), cost, [&](TaskRange range) -> Status {
        for (std::uint64_t y = range.begin; y < range.end; ++y)
        {
            const auto row = static_cast<std::uint32_t>(y);
            for (std::uint32_t channel = 0u; channel < channels; ++channel)
            {
                // A checked destination names one of the eight representations, so the dispatch always runs.
                (void)dispatchSampleType(sampleType, [&]<typename SampleT>() noexcept {
                    if (working.sampleType == SampleType::Float64)
                    {
                        writeRow<double, SampleT>(working.row<double>(channel, row), bytes, destinationLayout, channel,
                                                  row);
                    }
                    else
                    {
                        writeRow<float, SampleT>(working.row<float>(channel, row), bytes, destinationLayout, channel,
                                                 row);
                    }
                });
            }
        }
        return Status::success();
    });
}

} // namespace an::composition_detail

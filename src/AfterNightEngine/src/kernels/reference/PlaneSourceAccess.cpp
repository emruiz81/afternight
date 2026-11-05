// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the checks, partition and scratch ownership shared by plane algorithms.

#include "kernels/reference/PlaneSourceAccess.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace an::plane_detail
{
namespace
{

/// One failure from this shared layer, carrying the caller's operation name.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status planeFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a pointer is aligned for the representation it is read as.
///
/// @param pointer the first byte of the storage.
/// @param elementBytes the representation's width.
/// @return true when the address is a multiple of that width.
[[nodiscard]] bool pointerIsSampleAligned(const void* pointer, std::uint32_t elementBytes) noexcept
{
    return reinterpret_cast<std::uintptr_t>(pointer) % elementBytes == 0u;
}

} // namespace

Status checkPlaneSource(const ExecutionContext& execution, const KernelPlaneSource& source,
                        const KernelValidityView& validity, const char* operation)
{
    return checkPlaneSource(execution, source, validity, PlaneWindow{}, operation);
}

Status checkPlaneSource(const ExecutionContext& execution, const KernelPlaneSource& source,
                        const KernelValidityView& validity, const PlaneWindow& window, const char* operation)
{
    if (!execution.valid())
    {
        return planeFailure(ErrorCode::NotInitialized, operation, "the operation was given an empty execution context");
    }
    Status status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    const Result<BackendSelection> selected = selectBackend(execution);
    if (!selected.hasValue())
    {
        return selected.status();
    }
    if (source.samples == nullptr || source.layout == nullptr || !isSampleType(source.sampleType))
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the source has a null pointer, null layout, or unknown sample type");
    }
    if (source.layout->elementSizeBytes() != sampleTypeSizeBytes(source.sampleType) ||
        source.plane >= source.layout->planeCount() || source.sampleInPixel >= source.layout->samplesPerPixel())
    {
        return planeFailure(ErrorCode::IncompatibleArguments, operation,
                            "the source type or selected component does not match its layout");
    }
    if (!pointerIsSampleAligned(source.samples, source.layout->elementSizeBytes()))
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the source is not aligned for its resident sample type");
    }
    if (!validity.descriptor.needsMask())
    {
        if (validity.mask != nullptr || validity.maskPlane != 0u)
        {
            return planeFailure(ErrorCode::InvalidArgument, operation,
                                "a packed mask was supplied for a validity rule that does not use one");
        }
        return Status::success();
    }
    if (validity.mask == nullptr)
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the explicit-mask validity rule was given no packed mask");
    }
    const bool exactExtent = validity.mask->width() == source.layout->extent().width() &&
                             validity.mask->height() == source.layout->extent().height();
    const bool containsWindow = window.maskOriginX < validity.mask->width() &&
                                window.maskOriginY < validity.mask->height() &&
                                source.layout->extent().width() <= validity.mask->width() - window.maskOriginX &&
                                source.layout->extent().height() <= validity.mask->height() - window.maskOriginY;
    if ((window.requireExactMaskExtent ? !exactExtent : !containsWindow) ||
        validity.maskPlane >= validity.mask->planeCount())
    {
        return planeFailure(ErrorCode::IncompatibleArguments, operation,
                            "the packed mask does not cover the source extent and selected plane");
    }
    return Status::success();
}

bool maskAllows(const KernelValidityView& validity, std::uint32_t x, std::uint32_t y) noexcept
{
    return maskAllows(validity, PlaneWindow{}, x, y);
}

bool maskAllows(const KernelValidityView& validity, const PlaneWindow& window, std::uint32_t x,
                std::uint32_t y) noexcept
{
    return maskAllowsInline(validity, window, x, y);
}

Result<PlaneRectangle> describePlaneRectangle(const ExecutionContext& execution, const KernelPlaneSource& source,
                                              const KernelPlaneSelection& selection, const char* operation)
{
    if (selection.valueWindow.has_value() &&
        (!std::isfinite(selection.valueWindow->lowest) || !std::isfinite(selection.valueWindow->highest) ||
         selection.valueWindow->lowest > selection.valueWindow->highest))
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the value interval is not a finite ordered closed interval");
    }
    const std::uint32_t sourceWidth = source.layout->extent().width();
    const std::uint32_t sourceHeight = source.layout->extent().height();
    if (selection.originX > sourceWidth || selection.originY > sourceHeight ||
        selection.width > sourceWidth - selection.originX || selection.height > sourceHeight - selection.originY ||
        selection.width == 0u || selection.height == 0u)
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the selected rectangle is empty or extends outside the source image");
    }
    const std::optional<ImageExtent> rectangleExtent = ImageExtent::create(
        selection.width, selection.height, source.layout->extent().channelCount(), execution.limits());
    if (!rectangleExtent.has_value())
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the selected rectangle cannot be represented within the engine context limits");
    }
    const ImageLayoutRequest request{.extent = *rectangleExtent,
                                     .elementSizeBytes = source.layout->elementSizeBytes(),
                                     .samplesPerPixel = source.layout->samplesPerPixel(),
                                     .planeCount = source.layout->planeCount(),
                                     .rowAlignmentBytes = 1u};
    const std::optional<ImageLayout> rectangleLayout = ImageLayout::createWithStrides(
        request, source.layout->rowStrideBytes(), source.layout->planeStrideBytes(), execution.limits());
    if (!rectangleLayout.has_value())
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the selected rectangle is incompatible with the source image strides");
    }
    const std::optional<std::uint64_t> originOffset =
        source.layout->sampleByteOffset(0u, selection.originY, selection.originX, 0u);
    if (!originOffset.has_value())
    {
        return planeFailure(ErrorCode::InvalidArgument, operation,
                            "the selected rectangle has no address in the source image");
    }
    return PlaneRectangle{
        .layout = *rectangleLayout,
        .samples = static_cast<const std::byte*>(source.samples) + *originOffset,
        .window =
            PlaneWindow{.maskOriginX = selection.originX,
                        .maskOriginY = selection.originY,
                        .requireExactMaskExtent = false,
                        .hasValueWindow = selection.valueWindow.has_value(),
                        .lowestValue = selection.valueWindow.has_value() ? selection.valueWindow->lowest : 0.0,
                        .highestValue = selection.valueWindow.has_value() ? selection.valueWindow->highest : 0.0}};
}

TaskRange deterministicRange(std::uint64_t index, std::uint64_t itemCount, std::uint64_t chunkCount) noexcept
{
    const std::uint64_t base = itemCount / chunkCount;
    const std::uint64_t remainder = itemCount % chunkCount;
    const std::uint64_t extra = index < remainder ? index : remainder;
    const std::uint64_t begin = index * base + extra;
    return TaskRange{.begin = begin, .end = begin + base + (index < remainder ? 1u : 0u)};
}

ScopedContextMemory::ScopedContextMemory(MemoryResource& resource, void* pointer, std::uint64_t bytes,
                                         std::uint64_t alignment) noexcept
    : m_resource(&resource), m_pointer(pointer), m_bytes(bytes), m_alignment(alignment)
{
}

ScopedContextMemory::ScopedContextMemory(ScopedContextMemory&& other) noexcept
    : m_resource(std::exchange(other.m_resource, nullptr)), m_pointer(std::exchange(other.m_pointer, nullptr)),
      m_bytes(std::exchange(other.m_bytes, 0u)), m_alignment(std::exchange(other.m_alignment, 0u))
{
}

ScopedContextMemory& ScopedContextMemory::operator=(ScopedContextMemory&& other) noexcept
{
    if (this != &other)
    {
        if (m_resource != nullptr && m_pointer != nullptr)
        {
            m_resource->deallocate(m_pointer, m_bytes, m_alignment);
        }
        m_resource = std::exchange(other.m_resource, nullptr);
        m_pointer = std::exchange(other.m_pointer, nullptr);
        m_bytes = std::exchange(other.m_bytes, 0u);
        m_alignment = std::exchange(other.m_alignment, 0u);
    }
    return *this;
}

ScopedContextMemory::~ScopedContextMemory()
{
    if (m_resource != nullptr && m_pointer != nullptr)
    {
        m_resource->deallocate(m_pointer, m_bytes, m_alignment);
    }
}

Result<ScopedContextMemory> ScopedContextMemory::take(const ExecutionContext& execution, std::uint64_t bytes,
                                                      std::uint64_t alignment, const char* operation)
{
    if (bytes == 0u)
    {
        return ScopedContextMemory{};
    }
    MemoryResource* const resource = execution.memoryResource();
    if (resource == nullptr)
    {
        return planeFailure(ErrorCode::NotInitialized, operation, "the execution has no context memory resource");
    }
    Result<void*> allocated = resource->allocate(bytes, alignment);
    if (!allocated.hasValue())
    {
        return allocated.status();
    }
    return ScopedContextMemory{*resource, *allocated, bytes, alignment};
}

} // namespace an::plane_detail

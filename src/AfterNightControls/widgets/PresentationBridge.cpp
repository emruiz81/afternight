// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The presented-image owner and the one `QImage` wrapper.

#include "widgets/PresentationBridge.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <new>
#include <optional>
#include <string>

namespace an::ui
{

namespace
{

/// The format of a layout of one plane of one-byte samples, or nothing for any other layout.
[[nodiscard]] std::optional<PresentedFormat> formatOf(const an::ImageLayoutRequest& request) noexcept
{
    if (request.elementSizeBytes != 1u || request.planeCount != 1u ||
        request.samplesPerPixel != request.extent.channelCount())
    {
        return std::nullopt;
    }
    switch (request.samplesPerPixel)
    {
    case 1u:
        return PresentedFormat::Grey8;
    case 3u:
        return PresentedFormat::Rgb8;
    case 4u:
        return PresentedFormat::Rgba8;
    default:
        return std::nullopt;
    }
}

/// How many bytes a pixel of a format holds.
[[nodiscard]] std::uint32_t bytesPerPixelOf(PresentedFormat format) noexcept
{
    switch (format)
    {
    case PresentedFormat::Grey8:
        return 1u;
    case PresentedFormat::Rgb8:
        return 3u;
    case PresentedFormat::Rgba8:
    case PresentedFormat::Bgra8Premultiplied:
        return 4u;
    }
    return 0u;
}

/// Releases the presented image a `QImage` shared its bytes with, once Qt lets the last image sharing them go.
void releasePresentedImage(void* owner) noexcept
{
    delete static_cast<std::shared_ptr<const PresentedImage>*>(owner);
}

} // namespace

const char* presentedFormatName(PresentedFormat format) noexcept
{
    switch (format)
    {
    case PresentedFormat::Grey8:
        return "grey8";
    case PresentedFormat::Rgb8:
        return "rgb8";
    case PresentedFormat::Rgba8:
        return "rgba8";
    case PresentedFormat::Bgra8Premultiplied:
        return "bgra8-premultiplied";
    }
    return "unknown";
}

QImage::Format qtFormatOf(PresentedFormat format) noexcept
{
    switch (format)
    {
    case PresentedFormat::Grey8:
        return QImage::Format_Grayscale8;
    case PresentedFormat::Rgb8:
        return QImage::Format_RGB888;
    case PresentedFormat::Rgba8:
        return QImage::Format_RGBA8888;
    case PresentedFormat::Bgra8Premultiplied:
        return QImage::Format_ARGB32_Premultiplied;
    }
    return QImage::Format_Invalid;
}

PresentedImage::PresentedImage(const an::ImageLayout& layout, PresentedFormat format,
                               std::unique_ptr<std::uint8_t[]> bytes, an::ImageStorageBytes storage,
                               an::consumer::MemoryReservation reservation) noexcept
    : m_layout(layout), m_format(format), m_bytes(std::move(bytes)), m_storage(std::move(storage)),
      m_data(m_bytes != nullptr ? m_bytes.get() : static_cast<std::uint8_t*>(m_storage.data())),
      m_reservation(std::move(reservation))
{
}

PresentedImage::~PresentedImage() = default;

an::Result<std::shared_ptr<PresentedImage>> PresentedImage::allocate(const an::ImageLayoutRequest& request,
                                                                     const an::EngineLimits& limits,
                                                                     an::consumer::ConsumerMemory* memory)
{
    const std::optional<PresentedFormat> format = formatOf(request);
    if (!format.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "a presented image is one plane of one, three or four one-byte samples per pixel");
    }
    return make(nullptr, request, *format, limits, memory);
}

an::Result<std::shared_ptr<PresentedImage>> PresentedImage::allocate(const an::ImageLayoutRequest& request,
                                                                     const PresentedFormat format,
                                                                     const an::EngineLimits& limits,
                                                                     an::consumer::ConsumerMemory* memory)
{
    return make(nullptr, request, format, limits, memory);
}

an::Result<std::shared_ptr<PresentedImage>> PresentedImage::allocate(const an::EngineContext& engine,
                                                                     const an::ImageLayoutRequest& request,
                                                                     an::consumer::ConsumerMemory* memory)
{
    const std::optional<PresentedFormat> format = formatOf(request);
    if (!format.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "a presented image is one plane of one, three or four one-byte samples per pixel");
    }
    return make(&engine, request, *format, engine.limits(), memory);
}

an::Result<std::shared_ptr<PresentedImage>> PresentedImage::allocate(const an::EngineContext& engine,
                                                                     const an::ImageLayoutRequest& request,
                                                                     const PresentedFormat format,
                                                                     an::consumer::ConsumerMemory* memory)
{
    return make(&engine, request, format, engine.limits(), memory);
}

an::Result<std::shared_ptr<PresentedImage>>
PresentedImage::make(const an::EngineContext* engine, const an::ImageLayoutRequest& request,
                     const PresentedFormat format, const an::EngineLimits& limits, an::consumer::ConsumerMemory* memory)
{
    const std::optional<PresentedFormat> shape = formatOf(request);
    if (!shape.has_value() || bytesPerPixelOf(format) != request.samplesPerPixel)
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "a presented image is one plane of the bytes its format holds per pixel");
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    if (!layout.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "the presented image's layout is refused by the engine's limits");
    }
    // A `QImage` over the bytes reads every row whole, the last one included, so the last row's padding is allocated
    // even though the engine's layout stops at its last sample.
    const std::uint64_t allocated = layout->rowStrideBytes() * layout->extent().height();
    an::consumer::MemoryReservation reservation;
    if (memory != nullptr)
    {
        an::Result<an::consumer::MemoryReservation> reserved = memory->reserve(
            an::consumer::MemoryCategory::DisplayBuffers, allocated, an::consumer::ReservationKind::Required);
        if (!reserved.hasValue())
        {
            return reserved.status();
        }
        reservation = std::move(*reserved);
    }
    if (engine != nullptr)
    {
        // The engine's layout stops at the last sample, so storage for the bytes a `QImage` reads is laid out a row
        // taller when the last row has padding.
        std::optional<an::ImageLayout> covering = layout;
        if (layout->totalBytes() < allocated)
        {
            covering.reset();
            const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
                request.extent.width(), request.extent.height() + 1u, request.extent.channelCount(), limits);
            if (extent.has_value())
            {
                an::ImageLayoutRequest taller = request;
                taller.extent = *extent;
                covering = an::ImageLayout::create(taller, limits);
            }
        }
        if (covering.has_value())
        {
            an::Result<an::ImageStorageBytes> storage =
                an::ImageStorageBytes::create(*engine, *covering, an::StorageInitialization::Uninitialized);
            if (storage.hasValue())
            {
                return std::shared_ptr<PresentedImage>(
                    new PresentedImage(*layout, format, nullptr, std::move(*storage), std::move(reservation)));
            }
        }
    }
    std::unique_ptr<std::uint8_t[]> bytes(new (std::nothrow) std::uint8_t[allocated]);
    if (bytes == nullptr)
    {
        return an::Status::failure(an::ErrorCode::AllocationFailed, "the presented image's " +
                                                                        std::to_string(allocated) +
                                                                        " bytes could not be allocated");
    }
    return std::shared_ptr<PresentedImage>(
        new PresentedImage(*layout, format, std::move(bytes), an::ImageStorageBytes{}, std::move(reservation)));
}

const std::uint8_t* PresentedImage::pixel(std::uint32_t x, std::uint32_t y) const noexcept
{
    const std::optional<std::uint64_t> offset = m_layout.sampleByteOffset(0u, y, x, 0u);
    return offset.has_value() ? m_data + *offset : nullptr;
}

QImage presentedQImage(std::shared_ptr<const PresentedImage> image)
{
    if (image == nullptr)
    {
        return QImage();
    }
    const PresentedImage& shown = *image;
    auto* owner = new std::shared_ptr<const PresentedImage>(std::move(image));
    return QImage(shown.data(), static_cast<int>(shown.width()), static_cast<int>(shown.height()),
                  static_cast<qsizetype>(shown.layout().rowStrideBytes()), qtFormatOf(shown.format()),
                  &releasePresentedImage, owner);
}

} // namespace an::ui

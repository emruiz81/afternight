// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The one bridge between what the engine presents and what Qt paints.
///
/// The engine writes display bytes - a presentation's interleaved grey, RGB or red-green-blue-alpha bytes, a mask's
/// overlay layer - into memory its caller lays out and owns. Qt paints a `QImage`, and a `QImage` built over memory it
/// does not own reads that memory for as long as any copy of it lives: a queued paint, a pixmap made from it, a lens
/// that cropped it. So the memory has to outlive every one of them, whatever happens to the image it was presented
/// from - a resize, a replaced asset, a closed view.
///
/// **A `PresentedImage` owns the bytes.** It is allocated in the layout the engine states, counted against the
/// session's display-buffer allowance for as long as it lives, and shared: everything that paints from it holds a
/// shared pointer. A presentation takes its bytes from the engine context it ran in, whose memory a session keeps for
/// the next frame while a preview is open, so a view presented frame after frame reuses the bytes it let go.
///
/// **`presentedQImage` is the only way presented bytes become a `QImage`.** The image it returns shares the bytes and
/// carries a cleanup owner - a shared pointer to the presented image - that Qt releases when the last image sharing the
/// bytes is destroyed, so no paint can outlive what it paints. It is safe on any thread.
///
/// **A view paints the `QImage` itself.** A presentation is made at the size the view paints, in blue, green, red and
/// alpha bytes premultiplied by alpha - the format Qt's raster engine draws without converting - so the GUI thread
/// wraps the bytes and draws them: no pixmap is made of them and nothing is converted or scaled before the paint.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageStorage.h>

#include <QImage>

#include <cstdint>
#include <memory>

namespace an
{
class EngineContext;
} // namespace an

namespace an::ui
{

/// How the bytes of a presented image are arranged: one interleaved plane, rows starting on four-byte boundaries.
enum class PresentedFormat : std::uint8_t
{
    /// One byte per pixel.
    Grey8,

    /// Three bytes per pixel, red, green and blue.
    Rgb8,

    /// Four bytes per pixel, red, green, blue and straight alpha.
    Rgba8,

    /// Four bytes per pixel, blue, green, red and alpha, the colour premultiplied by alpha: one 32-bit premultiplied
    /// ARGB word on a little-endian host, which Qt paints without converting.
    Bgra8Premultiplied
};

/// A format's name, for messages and records.
///
/// @param format the format.
/// @return a static string; "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* presentedFormatName(PresentedFormat format) noexcept;

/// The Qt format a presented format is painted as.
///
/// @param format the format.
/// @return `QImage::Format_Grayscale8`, `Format_RGB888`, `Format_RGBA8888` or `Format_ARGB32_Premultiplied`;
///         `Format_Invalid` outside the enumeration.
[[nodiscard]] AFTERNIGHTCONTROLS_API QImage::Format qtFormatOf(PresentedFormat format) noexcept;

/// Display bytes the engine wrote, owned with their shape and the display allowance they count against.
///
/// Created whole by `allocate`, written once by the engine operation that presents into it, and read from then on:
/// share it as a `std::shared_ptr<const PresentedImage>`. Not copyable or movable, so a pointer into its bytes stays
/// valid for its whole life.
class PresentedImage
{
public:
    /// Allocates display bytes in the layout an engine operation states for its output.
    ///
    /// The bytes are counted against `memory`'s display-buffer allowance as a required buffer - a view that is
    /// painting cannot be refused its buffer without going blank - so an allowance that is exceeded is counted as an
    /// overrun rather than refused.
    ///
    /// @param request the layout the operation states: one plane of one-byte samples, one, three or four per pixel.
    /// @param limits the limits the layout is checked against: the engine context's.
    /// @param memory the session's consumer memory, or null to count nothing.
    /// @return the image, uninitialized; `ErrorCode::InvalidArgument` for a layout that is not one plane of one, three
    ///         or four one-byte samples per pixel or that the limits refuse; `ErrorCode::AllocationFailed` when the
    ///         bytes cannot be allocated.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<PresentedImage>>
    allocate(const an::ImageLayoutRequest& request, const an::EngineLimits& limits,
             an::consumer::ConsumerMemory* memory);

    /// Allocates display bytes of a stated format, for a layout whose pixel size does not tell it: four bytes are
    /// red-first with straight alpha unless stated otherwise.
    ///
    /// @param request the layout the operation states.
    /// @param format how the bytes are arranged, which must have the layout's samples per pixel.
    /// @param limits the limits the layout is checked against.
    /// @param memory the session's consumer memory, or null to count nothing.
    /// @return the image, uninitialized; `ErrorCode::InvalidArgument` for a layout `allocate` refuses or one of
    ///         another pixel size than the format's; `ErrorCode::AllocationFailed` when the bytes cannot be allocated.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<PresentedImage>>
    allocate(const an::ImageLayoutRequest& request, PresentedFormat format, const an::EngineLimits& limits,
             an::consumer::ConsumerMemory* memory);

    /// Allocates display bytes as `allocate` without a context does, from the memory of the engine context the
    /// presentation runs in, which the bytes keep open; the layout is checked against the context's limits. A context
    /// whose budget refuses the bytes leaves them to the platform's allocator, so the engine does not refuse a view
    /// that is painting its buffer either.
    ///
    /// @param engine the context of the operation that presents into the bytes.
    /// @param request the layout the operation states: one plane of one-byte samples, one, three or four per pixel.
    /// @param memory the session's consumer memory, or null to count nothing.
    /// @return as `allocate` without a context.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<PresentedImage>>
    allocate(const an::EngineContext& engine, const an::ImageLayoutRequest& request,
             an::consumer::ConsumerMemory* memory);

    /// Allocates display bytes of a stated format from the memory of an engine context, as the overload without a
    /// format does.
    ///
    /// @param engine the context of the operation that presents into the bytes.
    /// @param request the layout the operation states.
    /// @param format how the bytes are arranged, which must have the layout's samples per pixel.
    /// @param memory the session's consumer memory, or null to count nothing.
    /// @return as `allocate` without a context.
    [[nodiscard]] static AFTERNIGHTCONTROLS_API an::Result<std::shared_ptr<PresentedImage>>
    allocate(const an::EngineContext& engine, const an::ImageLayoutRequest& request, PresentedFormat format,
             an::consumer::ConsumerMemory* memory);

    /// Releases the bytes and their reservation.
    AFTERNIGHTCONTROLS_API ~PresentedImage();

    PresentedImage(const PresentedImage&) = delete;
    PresentedImage& operator=(const PresentedImage&) = delete;
    PresentedImage(PresentedImage&&) = delete;
    PresentedImage& operator=(PresentedImage&&) = delete;

    /// The width in pixels.
    [[nodiscard]] std::uint32_t width() const noexcept
    {
        return m_layout.extent().width();
    }

    /// The height in pixels.
    [[nodiscard]] std::uint32_t height() const noexcept
    {
        return m_layout.extent().height();
    }

    /// How the bytes are arranged.
    [[nodiscard]] PresentedFormat format() const noexcept
    {
        return m_format;
    }

    /// The layout the bytes are in: one interleaved plane, rows on four-byte boundaries.
    [[nodiscard]] const an::ImageLayout& layout() const noexcept
    {
        return m_layout;
    }

    /// The first byte, for reading.
    [[nodiscard]] const std::uint8_t* data() const noexcept
    {
        return m_data;
    }

    /// The first byte, for the operation that writes the image before it is shared.
    [[nodiscard]] std::uint8_t* data() noexcept
    {
        return m_data;
    }

    /// Whether the bytes came from an engine context's memory rather than the platform's allocator.
    [[nodiscard]] bool fromEngineContext() const noexcept
    {
        return m_storage.valid();
    }

    /// How many bytes the image holds: every row whole, the last row's padding included, because a `QImage` reads
    /// `bytesPerLine` bytes of every row.
    [[nodiscard]] std::uint64_t byteCount() const noexcept
    {
        return m_layout.rowStrideBytes() * m_layout.extent().height();
    }

    /// The bytes of one pixel, or null outside the image.
    ///
    /// @param x the column.
    /// @param y the row.
    /// @return the pixel's first byte.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const std::uint8_t* pixel(std::uint32_t x, std::uint32_t y) const noexcept;

    /// How many bytes each pixel holds: one, three or four.
    [[nodiscard]] std::uint32_t bytesPerPixel() const noexcept
    {
        return m_layout.samplesPerPixel();
    }

private:
    PresentedImage(const an::ImageLayout& layout, PresentedFormat format, std::unique_ptr<std::uint8_t[]> bytes,
                   an::ImageStorageBytes storage, an::consumer::MemoryReservation reservation) noexcept;

    /// What every `allocate` does: the bytes from the context when one is named and it grants them, from the
    /// platform's allocator otherwise.
    [[nodiscard]] static an::Result<std::shared_ptr<PresentedImage>>
    make(const an::EngineContext* engine, const an::ImageLayoutRequest& request, PresentedFormat format,
         const an::EngineLimits& limits, an::consumer::ConsumerMemory* memory);

    an::ImageLayout m_layout;
    PresentedFormat m_format;

    /// The bytes, held by exactly one of the two: the platform's allocation, or the context's storage.
    std::unique_ptr<std::uint8_t[]> m_bytes;
    an::ImageStorageBytes m_storage;
    std::uint8_t* m_data = nullptr;

    an::consumer::MemoryReservation m_reservation;
};

/// Wraps presented bytes in a `QImage` that shares them.
///
/// The image, and every copy and pixmap made from it, keeps the presented image alive; the presented image is released
/// when the last of them is destroyed. Any thread.
///
/// @param image the presented image; null gives a null `QImage`.
/// @return the image, read-only: writing to it detaches it from the presented bytes, as any `QImage` over borrowed
///         memory does.
[[nodiscard]] AFTERNIGHTCONTROLS_API QImage presentedQImage(std::shared_ptr<const PresentedImage> image);

} // namespace an::ui

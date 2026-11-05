// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/RawFrame.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/SampleBuffer.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

// Three decisions in this file are worth naming, because none of them is visible from the header.
//
// **Construction checks three things beyond the buffer holding bytes at all, and nothing else.**
// That the layout describes samples of the descriptor's width, that the two agree about how many
// pixels there are, and that the buffer is at least as large as the layout says. Everything after
// that - a row, a plane, the whole region - is arithmetic on numbers already proved to fit, which is
// the same bargain `ImageView` makes and for the same reason: a boundary that re-derives its bounds
// per row is a boundary that pays for its safety in the inner loop.
//
// **The buffer is only consumed when the frame is built.** A refused `create` leaves the caller
// holding its bytes, which matters here more than it does for engine memory: a decoder-owned buffer
// that vanished on a failed description would take the decoder's data with it, and the caller would
// have nothing left to report the failure about.
//
// **Adoption is refused for three separate reasons and says which.** A representation outside the
// eight needs a conversion, foreign byte order needs a swap, and memory the engine does not own
// needs a copy into memory it does. Collapsing them into one refusal would leave every caller
// re-deriving which of the three it was in order to know what to do next.

namespace an
{
namespace
{

/// The refusal for a layout and a description that are not about the same frame.
///
/// @param error the geometry rule that names the disagreement.
/// @param message what disagreed, for a person.
/// @return an `ErrorCode::IncompatibleArguments` failure carrying the rule's own name.
[[nodiscard]] Status refusePairing(GeometryError error, const char* message)
{
    return Status::failure(ErrorCode::IncompatibleArguments,
                           StatusDetail{.message = std::string(message) + ": " + geometryErrorName(error),
                                        .operation = "RawFrame::create",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, "layout")}});
}

} // namespace

RawFrame::RawFrame(const RawFrameDescriptor& descriptor, const ImageLayout& layout, SampleBuffer&& buffer) noexcept
    : m_descriptor(descriptor), m_layout(layout), m_buffer(std::move(buffer))
{
}

Result<RawFrame> RawFrame::create(const RawFrameDescriptor& descriptor, const ImageLayout& layout,
                                  SampleBuffer&& buffer)
{
    if (!buffer.valid())
    {
        return Status::failure(ErrorCode::NotInitialized,
                               StatusDetail{.message = "a frame needs bytes, and this buffer holds none",
                                            .operation = "RawFrame::create",
                                            .domain = ErrorDomain::Image,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "buffer")}});
    }
    if (layout.elementSizeBytes() != descriptor.sampleFormat().sizeBytes)
    {
        return refusePairing(GeometryError::ElementSizeMismatch,
                             "the layout describes samples of a width the frame does not have");
    }
    if (!(layout.extent() == descriptor.extent()))
    {
        // Deliberately not one of the geometry reasons: every one of those is about a layout on its
        // own, and this is two descriptions of one frame that do not agree. Naming the nearest
        // geometry rule would send a reader to the layout to find a fault that is not in it.
        return Status::failure(ErrorCode::IncompatibleArguments,
                               StatusDetail{.message = "the layout and the frame describe different numbers of pixels",
                                            .operation = "RawFrame::create",
                                            .domain = ErrorDomain::Image,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "layout")}});
    }

    const std::uint64_t requiredBytes = layout.totalBytes();
    const std::uint64_t availableBytes = buffer.byteCount();
    if (availableBytes < requiredBytes)
    {
        return Status::failure(
            ErrorCode::OutOfRange,
            StatusDetail{.message = "the buffer holds fewer bytes than the layout describes",
                         .operation = "RawFrame::create",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "buffer"),
                                     StatusContextField::ofCount(ContextKey::Requested, requiredBytes),
                                     StatusContextField::ofCount(ContextKey::Available, availableBytes)}});
    }

    return RawFrame{descriptor, layout, std::move(buffer)};
}

bool RawFrame::valid() const noexcept
{
    return m_buffer.valid() && m_descriptor.has_value();
}

std::optional<RawFrameDescriptor> RawFrame::descriptor() const noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    return m_descriptor;
}

std::optional<ImageLayout> RawFrame::layout() const noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    return m_layout;
}

const SampleBuffer& RawFrame::buffer() const noexcept
{
    return m_buffer;
}

std::span<const std::byte> RawFrame::bytes() const noexcept
{
    return m_buffer.bytes();
}

std::optional<std::span<std::byte>> RawFrame::mutableBytes() noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    return m_buffer.mutableBytes();
}

std::optional<std::span<const std::byte>> RawFrame::row(std::uint32_t planeIndex, std::uint32_t y) const noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    const std::optional<std::uint64_t> offset = m_layout->rowByteOffset(planeIndex, y);
    if (!offset.has_value())
    {
        return std::nullopt;
    }
    const std::span<const std::byte> whole = m_buffer.bytes();
    return whole.subspan(static_cast<std::size_t>(*offset), static_cast<std::size_t>(m_layout->minimumRowBytes()));
}

std::optional<std::span<std::byte>> RawFrame::mutableRow(std::uint32_t planeIndex, std::uint32_t y) noexcept
{
    if (!valid())
    {
        return std::nullopt;
    }
    const std::optional<std::uint64_t> offset = m_layout->rowByteOffset(planeIndex, y);
    if (!offset.has_value())
    {
        return std::nullopt;
    }
    const std::optional<std::span<std::byte>> whole = m_buffer.mutableBytes();
    if (!whole.has_value())
    {
        return std::nullopt;
    }
    return whole->subspan(static_cast<std::size_t>(*offset), static_cast<std::size_t>(m_layout->minimumRowBytes()));
}

Status RawFrame::checkAdoptable() const
{
    if (!valid())
    {
        return Status::failure(ErrorCode::NotInitialized, StatusDetail{.message = "this frame holds no bytes to adopt",
                                                                       .operation = "RawFrame::checkAdoptable",
                                                                       .domain = ErrorDomain::Image});
    }
    if (!m_descriptor->residentSampleType().has_value())
    {
        return Status::failure(
            ErrorCode::Unsupported,
            StatusDetail{.message = std::string("this frame holds ") +
                                    std::to_string(m_descriptor->sampleFormat().sizeBytes) + "-byte " +
                                    rawSampleClassName(m_descriptor->sampleFormat().sampleClass) +
                                    " samples, which is not one of the eight representations this engine stores; it "
                                    "needs a conversion rather than an adoption",
                         .operation = "RawFrame::checkAdoptable",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "sampleFormat")}});
    }
    if (!m_descriptor->isHostOrder())
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = std::string("this frame holds ") + byteOrderName(m_descriptor->byteOrder()) +
                                    " samples on a " + byteOrderName(nativeByteOrder()) +
                                    " host; the bytes need swapping before they are an image",
                         .operation = "RawFrame::checkAdoptable",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "byteOrder")}});
    }
    if (!m_buffer.adoptable())
    {
        return Status::failure(
            ErrorCode::InvalidState,
            StatusDetail{.message = std::string("these bytes are ") + sampleBufferOwnershipName(m_buffer.ownership()) +
                                    " rather than engine memory, so an image can copy them but cannot take them over",
                         .operation = "RawFrame::checkAdoptable",
                         .domain = ErrorDomain::Image,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, "ownership")}});
    }
    return Status::success();
}

Result<AdoptedFrame> RawFrame::releaseAdoptable()
{
    const Status adoptable = checkAdoptable();
    if (adoptable.failed())
    {
        return adoptable;
    }

    const std::optional<ImageDescriptor> resident = m_descriptor->toImageDescriptor();
    if (!resident.has_value())
    {
        return Status::failure(ErrorCode::Internal,
                               StatusDetail{.message = "the frame names a resident representation and then refuses to "
                                                       "describe itself as a resident image",
                                            .operation = "RawFrame::releaseAdoptable",
                                            .domain = ErrorDomain::Image});
    }

    Result<ImageStorageBytes> storage = m_buffer.releaseOwned();
    if (!storage.hasValue())
    {
        return storage.status();
    }

    AdoptedFrame adopted{.descriptor = *resident, .layout = *m_layout, .storage = std::move(*storage)};
    m_descriptor.reset();
    m_layout.reset();
    m_buffer.release();
    return adopted;
}

void RawFrame::release() noexcept
{
    m_descriptor.reset();
    m_layout.reset();
    m_buffer.release();
}

} // namespace an

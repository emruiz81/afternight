// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A whole image's samples, borrowed rather than owned, checked once.
///
/// A plane view answers where one rectangle of samples is. An image view answers where all of them
/// are, which is one more distance - the bytes from one plane to the next - and one more question a
/// caller has to be stopped from getting wrong: how many planes there are, and how many of a pixel's
/// samples each of them holds. Those two numbers are the difference between an image stored a plane
/// per channel and one stored with its channels interleaved, and every kernel that walks an image
/// has to know which it was handed.
///
/// It is the layout that answers, not this type. A view pairs a validated `Core/ImageLayout.h` with
/// a typed pointer and proves the two go together: the layout's element size is the size of this
/// view's sample type, the buffer holds every byte the layout names, and the address the samples
/// start at is one a sample of that type may be read from. After that the strides are arithmetic
/// somebody already checked, which is what makes `plane`, `row` and `subview` free of everything
/// except a bounds test.
///
/// Nothing here allocates, resizes, converts or changes what an image means. A view is a way of
/// reaching samples, and an operation that changes their meaning builds a new descriptor beside them
/// rather than mutating one through a borrowed handle. That is why this type has no descriptor in
/// it at all: it borrows memory, and memory is all it claims to describe.

#pragma once

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace an
{

/// A whole image's samples, borrowed from whoever owns the memory.
///
/// There is no way to build one that has not been checked: the constructor is private and the
/// static entry points below are the only way in. So a function taking an `ImageView` may address
/// any plane, row or sample its extent names without re-deriving whether the buffer holds it.
///
/// Trivially copyable, and deliberately without a default constructor, for the reason `PlaneView`
/// has none: an image of no samples is not a thing this engine has, and a null-pointer view would
/// put a branch in every entry point below. A member assigned later is an
/// `std::optional<ImageView>`.
///
/// @tparam SampleT one of the eight resident sample types, or a const-qualified one of them.
template <ViewableSample SampleT> class ImageView
{
public:
    /// The type of one sample, const-qualified exactly when this view is read-only.
    using Sample = SampleT;

    /// Which representation this view's samples are, as a value.
    static constexpr SampleType kSampleType = kSampleTypeOf<SampleT>;

    /// Whether a buffer and a layout describe an image of this sample type, and if not, why not.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param samples the buffer the image lives in. It must hold every byte the layout names,
    ///        counting the padding between rows and planes but not any padding after the last row.
    /// @param layout where the samples sit, already validated as geometry.
    /// @return `GeometryError::None` when a view can be built; `ElementSizeMismatch` when the
    ///         layout describes samples of a different size than `SampleT`; `BufferTooSmall` or
    ///         `BaseAddressMisaligned` when the buffer cannot carry it.
    [[nodiscard]] static GeometryError check(std::span<SampleT> samples, const ImageLayout& layout) noexcept
    {
        if (layout.elementSizeBytes() != sizeof(SampleT))
        {
            return GeometryError::ElementSizeMismatch;
        }
        if (static_cast<std::uint64_t>(samples.size()) * sizeof(SampleT) < layout.totalBytes())
        {
            return GeometryError::BufferTooSmall;
        }
        if (!isPointerAligned(samples.data(), alignof(SampleT)))
        {
            return GeometryError::BaseAddressMisaligned;
        }
        return GeometryError::None;
    }

    /// A view of an image, where the buffer and the layout describe one.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param samples the buffer the image lives in, which must outlive every view of it.
    /// @param layout where the samples sit, already validated as geometry.
    /// @return the view, or an empty optional exactly when `check` would name a reason.
    [[nodiscard]] static std::optional<ImageView> create(std::span<SampleT> samples, const ImageLayout& layout) noexcept
    {
        if (check(samples, layout) != GeometryError::None)
        {
            return std::nullopt;
        }
        return ImageView{samples.data(), layout};
    }

    /// Adopts a writable image as a read-only one.
    ///
    /// Implicit, and available only in this direction, exactly as `PlaneView`'s is.
    ///
    /// @tparam OtherT the writable sample type, which must be this view's type without its const.
    /// @param writable the view to borrow from.
    template <typename OtherT>
        requires(std::is_const_v<SampleT> && std::is_same_v<std::remove_const_t<SampleT>, OtherT>)
    constexpr ImageView(const ImageView<OtherT>& writable) noexcept
        : m_data(writable.data()), m_layout(writable.layout())
    {
    }

    /// This image as a read-only view of the same samples.
    ///
    /// The named form of the conversion above, for a call site where the target type is deduced and
    /// the implicit conversion therefore does not happen.
    ///
    /// @return a view of the same memory through which nothing can be written.
    [[nodiscard]] constexpr ImageView<const std::remove_const_t<SampleT>> constView() const noexcept
    {
        return ImageView<const std::remove_const_t<SampleT>>::createChecked(m_data, m_layout);
    }

    /// Where the image's first sample is.
    ///
    /// @return the base address, never null, aligned for a sample of this type.
    [[nodiscard]] constexpr SampleT* data() const noexcept
    {
        return m_data;
    }

    /// Where the samples sit.
    ///
    /// @return the validated layout, whose element size is `sizeof(SampleT)`.
    [[nodiscard]] constexpr const ImageLayout& layout() const noexcept
    {
        return m_layout;
    }

    /// The image's dimensions.
    ///
    /// @return the extent, whose channel count is `samplesPerPixel() * planeCount()`.
    [[nodiscard]] constexpr const ImageExtent& extent() const noexcept
    {
        return m_layout.extent();
    }

    /// How many pixels wide the image is.
    ///
    /// @return the width, never zero.
    [[nodiscard]] constexpr std::uint32_t width() const noexcept
    {
        return m_layout.extent().width();
    }

    /// How many rows the image has.
    ///
    /// @return the height, never zero.
    [[nodiscard]] constexpr std::uint32_t height() const noexcept
    {
        return m_layout.extent().height();
    }

    /// How many channels the image has.
    ///
    /// @return the channel count, never zero.
    [[nodiscard]] constexpr std::uint32_t channelCount() const noexcept
    {
        return m_layout.extent().channelCount();
    }

    /// How many planes the samples are spread across.
    ///
    /// @return the plane count, never zero.
    [[nodiscard]] constexpr std::uint32_t planeCount() const noexcept
    {
        return m_layout.planeCount();
    }

    /// How many of one pixel's samples share a plane.
    ///
    /// @return the samples per pixel, never zero.
    [[nodiscard]] constexpr std::uint32_t samplesPerPixel() const noexcept
    {
        return m_layout.samplesPerPixel();
    }

    /// Distance in bytes from one row to the next within a plane.
    ///
    /// @return the stride exactly as it was stated, never normalised.
    [[nodiscard]] constexpr std::uint64_t rowStrideBytes() const noexcept
    {
        return m_layout.rowStrideBytes();
    }

    /// Distance in bytes from one plane to the next.
    ///
    /// @return the stride exactly as it was stated, never normalised.
    [[nodiscard]] constexpr std::uint64_t planeStrideBytes() const noexcept
    {
        return m_layout.planeStrideBytes();
    }

    /// How many samples one row of one plane holds, without the padding after them.
    ///
    /// @return width times samples per pixel.
    [[nodiscard]] constexpr std::uint64_t rowSampleCount() const noexcept
    {
        return static_cast<std::uint64_t>(width()) * static_cast<std::uint64_t>(samplesPerPixel());
    }

    /// How many samples the image holds.
    ///
    /// @return width times height times channel count, which is what a full pass visits.
    [[nodiscard]] constexpr std::uint64_t sampleCount() const noexcept
    {
        return m_layout.extent().sampleCount();
    }

    /// Whether the samples occupy the buffer with no gaps at all.
    ///
    /// The question to ask before treating the whole image as one run. It is a proof rather than a
    /// hint: when it is true, `contiguousSpan` returns the run.
    ///
    /// @return true when there is no padding between rows and none between planes.
    [[nodiscard]] constexpr bool isContiguous() const noexcept
    {
        return m_layout.isContiguous();
    }

    /// One plane, as the view a kernel body is written against.
    ///
    /// @param planeIndex the plane, below `planeCount()`.
    /// @return the plane's view, or an empty optional when `planeIndex` is out of range.
    [[nodiscard]] std::optional<PlaneView<SampleT>> plane(std::uint32_t planeIndex) const noexcept
    {
        if (planeIndex >= planeCount())
        {
            return std::nullopt;
        }
        const std::uint64_t consumed = static_cast<std::uint64_t>(planeIndex) * m_layout.planeStrideBytes();
        const std::uint64_t remaining = (m_layout.totalBytes() - consumed) / sizeof(SampleT);
        const std::span<SampleT> samples{m_data + planeIndex * planeElementStride(),
                                         static_cast<std::size_t>(remaining)};
        return PlaneView<SampleT>::create(samples, width(), height(), samplesPerPixel(), rowStrideBytes());
    }

    /// The samples of one row of one plane.
    ///
    /// What comes back covers the row's own samples and stops before whatever padding follows them,
    /// so writing to its end cannot reach the next row or the next plane.
    ///
    /// @param planeIndex the plane, below `planeCount()`.
    /// @param y the row, below `height()`.
    /// @return a span of `rowSampleCount()` samples, or an empty optional when either index is out
    ///         of range.
    [[nodiscard]] std::optional<std::span<SampleT>> row(std::uint32_t planeIndex, std::uint32_t y) const noexcept
    {
        if (planeIndex >= planeCount() || y >= height())
        {
            return std::nullopt;
        }
        return std::span<SampleT>{m_data + planeIndex * planeElementStride() +
                                      static_cast<std::uint64_t>(y) * rowElementStride(),
                                  static_cast<std::size_t>(rowSampleCount())};
    }

    /// The whole image as one run, when it is one.
    ///
    /// @return a span of `sampleCount()` samples when `isContiguous()`, and an empty optional
    ///         otherwise. It is empty rather than a best effort on purpose: a caller that treats a
    ///         padded image as a run reads the padding as samples and writes samples into it.
    [[nodiscard]] std::optional<std::span<SampleT>> contiguousSpan() const noexcept
    {
        if (!isContiguous())
        {
            return std::nullopt;
        }
        return std::span<SampleT>{m_data, static_cast<std::size_t>(sampleCount())};
    }

    /// One sample, addressed and bounds-checked.
    ///
    /// For tests, parsers and cold code. A kernel takes a row once and walks it; calling this per
    /// sample pays for four bounds tests on every one of them.
    ///
    /// @param planeIndex the plane, below `planeCount()`.
    /// @param x the column, below `width()`.
    /// @param y the row, below `height()`.
    /// @param sampleInPixel which of the pixel's samples in this plane, below `samplesPerPixel()`.
    /// @return the sample's address, or null when any index is out of range.
    [[nodiscard]] SampleT* sampleAt(std::uint32_t planeIndex, std::uint32_t x, std::uint32_t y,
                                    std::uint32_t sampleInPixel = 0u) const noexcept
    {
        if (planeIndex >= planeCount() || x >= width() || y >= height() || sampleInPixel >= samplesPerPixel())
        {
            return nullptr;
        }
        return m_data + planeIndex * planeElementStride() + static_cast<std::uint64_t>(y) * rowElementStride() +
               (static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(samplesPerPixel()) +
                static_cast<std::uint64_t>(sampleInPixel));
    }

    /// Whether a region of this image can be viewed on its own, and if not, why not.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param originX the region's first column, in pixels from the left.
    /// @param originY the region's first row.
    /// @param regionWidth the region's width in pixels.
    /// @param regionHeight the region's height in pixels.
    /// @return `GeometryError::None` when a view can be built; `RegionOutOfBounds` when the region
    ///         is not wholly inside this image; otherwise the rule that refused.
    [[nodiscard]] GeometryError checkSubview(std::uint32_t originX, std::uint32_t originY, std::uint32_t regionWidth,
                                             std::uint32_t regionHeight) const noexcept
    {
        if (regionWidth == 0u || regionHeight == 0u)
        {
            return GeometryError::ZeroDimension;
        }
        if (regionWidth > width() || regionHeight > height() || originX > width() - regionWidth ||
            originY > height() - regionHeight)
        {
            return GeometryError::RegionOutOfBounds;
        }
        const EngineLimits limits = viewGeometryLimits(regionWidth, regionHeight, channelCount());
        const GeometryError extentError = ImageExtent::check(regionWidth, regionHeight, channelCount(), limits);
        if (extentError != GeometryError::None)
        {
            return extentError;
        }
        const std::optional<ImageExtent> extent =
            ImageExtent::create(regionWidth, regionHeight, channelCount(), limits);
        if (!extent.has_value())
        {
            return GeometryError::ArithmeticOverflow;
        }
        return ImageLayout::checkWithStrides(subviewRequest(*extent), m_layout.rowStrideBytes(),
                                             m_layout.planeStrideBytes(), limits);
    }

    /// A view of a region of every plane of this image, sharing its memory.
    ///
    /// Both strides are kept, which is what makes a subview free: the samples do not move, and only
    /// the first address and the extent change. A region narrower than the image it came from is
    /// therefore padded even when that image was contiguous, and it reports so. Every plane is
    /// cropped identically - there is no subview that keeps some channels and drops others, because
    /// dropping a channel changes what the image is and belongs to an operation with a descriptor.
    ///
    /// @param originX the region's first column, in pixels from the left.
    /// @param originY the region's first row.
    /// @param regionWidth the region's width in pixels.
    /// @param regionHeight the region's height in pixels.
    /// @return the view, or an empty optional exactly when `checkSubview` would name a reason.
    [[nodiscard]] std::optional<ImageView> subview(std::uint32_t originX, std::uint32_t originY,
                                                   std::uint32_t regionWidth, std::uint32_t regionHeight) const noexcept
    {
        if (checkSubview(originX, originY, regionWidth, regionHeight) != GeometryError::None)
        {
            return std::nullopt;
        }
        const EngineLimits limits = viewGeometryLimits(regionWidth, regionHeight, channelCount());
        const std::optional<ImageExtent> extent =
            ImageExtent::create(regionWidth, regionHeight, channelCount(), limits);
        if (!extent.has_value())
        {
            return std::nullopt;
        }
        const std::optional<ImageLayout> layout = ImageLayout::createWithStrides(
            subviewRequest(*extent), m_layout.rowStrideBytes(), m_layout.planeStrideBytes(), limits);
        if (!layout.has_value())
        {
            return std::nullopt;
        }
        SampleT* const origin = m_data + static_cast<std::uint64_t>(originY) * rowElementStride() +
                                static_cast<std::uint64_t>(originX) * static_cast<std::uint64_t>(samplesPerPixel());
        return ImageView{origin, *layout};
    }

    /// Two image views are equal when they describe the same samples in the same place.
    ///
    /// Identity rather than content: two views of different buffers holding equal samples are not
    /// equal, and are not meant to be.
    friend constexpr bool operator==(const ImageView& left, const ImageView& right) noexcept = default;

    /// Builds a view whose buffer and geometry have already been proved to go together.
    ///
    /// Public because one instantiation of this template cannot reach another's private members,
    /// and the const conversion has to hand over a pair that is already checked, and because an
    /// owner that allocated the buffer for this exact layout has proved everything `check` asks.
    /// It is not the entry point for a caller holding a raw pointer: `create` is, and it checks.
    ///
    /// @param data the base address, already proved non-null and aligned for this type.
    /// @param layout the image's layout, already proved to be of this element size and to fit
    ///        within the buffer.
    /// @return the view.
    [[nodiscard]] static constexpr ImageView createChecked(SampleT* data, const ImageLayout& layout) noexcept
    {
        return ImageView{data, layout};
    }

private:
    /// Builds the value once the buffer and the layout have been checked.
    ///
    /// @param data the checked base address.
    /// @param layout the checked layout.
    constexpr ImageView(SampleT* data, const ImageLayout& layout) noexcept : m_data(data), m_layout(layout) {}

    /// How many samples one row is from the next.
    ///
    /// @return the row stride in samples, which the layout proved is a whole number of them.
    [[nodiscard]] constexpr std::uint64_t rowElementStride() const noexcept
    {
        return m_layout.rowStrideBytes() / sizeof(SampleT);
    }

    /// How many samples one plane is from the next.
    ///
    /// @return the plane stride in samples, which the layout proved is a whole number of them.
    [[nodiscard]] constexpr std::uint64_t planeElementStride() const noexcept
    {
        return m_layout.planeStrideBytes() / sizeof(SampleT);
    }

    /// The layout request a cropped region of this image is built from.
    ///
    /// The arrangement and the element size are this image's; only the extent changes. The
    /// alignment is one because a crop moves the first sample by a column, which no alignment
    /// survives - the strides it keeps are still whole numbers of samples, which is what a typed
    /// pointer into the region actually needs.
    ///
    /// @param extent the region's dimensions.
    /// @return the request, to be checked against the strides this image already has.
    [[nodiscard]] constexpr ImageLayoutRequest subviewRequest(const ImageExtent& extent) const noexcept
    {
        return ImageLayoutRequest{.extent = extent,
                                  .elementSizeBytes = m_layout.elementSizeBytes(),
                                  .samplesPerPixel = m_layout.samplesPerPixel(),
                                  .planeCount = m_layout.planeCount(),
                                  .rowAlignmentBytes = 1u};
    }

    /// Where the image's first sample is. Never null.
    SampleT* m_data;

    /// The image's checked geometry, whose element size is `sizeof(SampleT)`.
    ImageLayout m_layout;
};

/// Whether two image views share a byte.
///
/// @tparam LeftT the first view's sample type.
/// @tparam RightT the second view's sample type.
/// @param left the first view.
/// @param right the second view.
/// @return true when the two images share at least one byte of memory, on the conservative terms
///         `regionsOverlap` describes. Two views of the same image report true, which is what makes
///         this usable as an in-place check as well as an aliasing one.
template <ViewableSample LeftT, ViewableSample RightT>
[[nodiscard]] inline bool viewsOverlap(const ImageView<LeftT>& left, const ImageView<RightT>& right) noexcept
{
    return regionsOverlap(left.data(), left.layout().totalBytes(), right.data(), right.layout().totalBytes());
}

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One plane of an image, borrowed rather than owned, checked once.
///
/// This is the object a kernel body should be written against. A plane is a rectangle of samples
/// with one distance in it - the bytes from one row to the next - and once that distance and the
/// buffer's size have been checked together, walking the plane needs no further validation at all.
/// The whole point of the type is to move that check out of the loop: a kernel takes a plane, asks
/// for a row, and gets a span it may read to its end.
///
/// It is a distinct type from `ImageView` rather than a special case of one, and the reason is a
/// specific defect. An image stored a plane per channel handed to code written for a single plane
/// processes its first channel and reports success, and there is nothing in the numbers to say so -
/// the strides are right, the extent is right, and two thirds of the image are simply never touched.
/// Two types make that a compile error. The conversion exists in one direction only: `ImageView`
/// hands out a plane, and nothing turns a plane back into an image.
///
/// Constness lives in the sample type, as it does in `std::span`. `PlaneView<const float>` converts
/// from `PlaneView<float>` implicitly and never the other way, so a function that takes a read-only
/// plane says so in its signature and cannot be handed a writable one by accident - or rather, can
/// be handed one, and cannot write through it.
///
/// A view knows nothing about who owns the memory. That is deliberate: a plane over an owned
/// storage, over a decoder's buffer and over a stack array are the same object, and the lifetime is
/// the caller's to reason about. A view outliving its buffer is the one mistake this type cannot
/// catch, which is why every entry point that produces one names what it borrows from.

#pragma once

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace an
{

/// The maxima a view checks its own geometry against.
///
/// A view describes memory that already exists, so the configured maximum image size has nothing
/// left to refuse: the allocation it guards happened in whatever produced the buffer, and refusing
/// to *describe* a buffer that is already there would leave a caller holding memory it cannot name.
/// The dimensional maxima are the region's own numbers for the same reason.
///
/// What these limits do not relax is representability. A geometry whose byte count overflows or
/// cannot be spanned by a pointer difference is refused whatever the configuration says, because
/// those refusals sit above the policy line in `Core/GeometryError.h`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param width the region's width in pixels.
/// @param height the region's height in pixels.
/// @param channelCount how many samples of one pixel the region holds.
/// @return limits that admit exactly this geometry and impose no byte maximum. A zero dimension is
///         still refused, by the extent, before any of these numbers is read.
[[nodiscard]] constexpr EngineLimits viewGeometryLimits(std::uint32_t width, std::uint32_t height,
                                                        std::uint32_t channelCount) noexcept
{
    return EngineLimits{.maxImageWidth = width,
                        .maxImageHeight = height,
                        .maxChannelCount = channelCount,
                        .maxImageBytes = (std::numeric_limits<std::uint64_t>::max)(),
                        .memoryBudgetBytes = (std::numeric_limits<std::uint64_t>::max)()};
}

/// Whether two regions of memory share a byte.
///
/// The question a kernel asks before writing its output through a view that might be its input.
/// Aliasing is not always wrong - an in-place pass is exactly the case where the two are the same
/// memory on purpose - so this reports the fact and leaves the rule to whatever declared what it
/// supports.
///
/// The answer is conservative in one direction and exact in the other. Two regions that share no
/// byte are always reported apart; two that share a byte are always reported overlapping, even when
/// the strides mean no sample of one ever lands on a sample of the other. A checkerboard of
/// interleaved planes therefore reports an overlap it could in principle survive, which is the
/// error a caller can detect and recover from rather than the one it cannot.
///
/// Thread-safe and allocation-free. Not usable in a constant expression: comparing addresses from
/// two unrelated objects is a run-time question, and answering it needs the addresses as integers.
///
/// @tparam LeftT the first region's resident sample type.
/// @tparam RightT the second region's resident sample type.
/// @param left the first region's first byte; may be null, which overlaps nothing.
/// @param leftBytes how many bytes the first region spans.
/// @param right the second region's first byte; may be null.
/// @param rightBytes how many bytes the second region spans.
/// @return true when the two regions share at least one byte. An empty region overlaps nothing,
///         including itself.
template <ViewableSample LeftT, ViewableSample RightT>
[[nodiscard]] inline bool regionsOverlap(const LeftT* left, std::uint64_t leftBytes, const RightT* right,
                                         std::uint64_t rightBytes) noexcept
{
    if (left == nullptr || right == nullptr || leftBytes == 0u || rightBytes == 0u)
    {
        return false;
    }
    const std::uintptr_t leftBegin = reinterpret_cast<std::uintptr_t>(left);
    const std::uintptr_t rightBegin = reinterpret_cast<std::uintptr_t>(right);
    return leftBegin < rightBegin + rightBytes && rightBegin < leftBegin + leftBytes;
}

/// One plane of an image: a rectangle of samples with one row distance, borrowed from whoever owns
/// the memory.
///
/// There is no way to build one that has not been checked: the constructor is private and the
/// static entry points below are the only way in. So a function taking a `PlaneView` may compute a
/// row address from it and read the row to its end, because construction proved the buffer holds
/// every byte the geometry names, that the base address is one a sample of this type may be read
/// from, and that every row after the first lands on the same kind of boundary.
///
/// Trivially copyable, and deliberately without a default constructor: there is no plane of no
/// samples, and a default-constructed view would be a null pointer that every entry point below
/// would then have to check for. A member that is assigned later is an `std::optional<PlaneView>`.
///
/// @tparam SampleT one of the eight resident sample types, or a const-qualified one of them.
template <ViewableSample SampleT> class PlaneView
{
public:
    /// The type of one sample, const-qualified exactly when this view is read-only.
    using Sample = SampleT;

    /// Which representation this view's samples are, as a value.
    static constexpr SampleType kSampleType = kSampleTypeOf<SampleT>;

    /// Whether a plane can be described this way, and if not, why not.
    ///
    /// This is the entry point for a caller that has to report the reason - to a user, or as the
    /// context of a failure it returns. A caller that only needs the value uses `create`.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param samples the buffer the plane lives in. It must hold every byte the geometry names,
    ///        counting the padding between rows but not any padding after the last row.
    /// @param width the plane's width in pixels.
    /// @param height the plane's height in pixels.
    /// @param samplesPerPixel how many of one pixel's samples this plane holds: one for a plane per
    ///        channel, the channel count for an image whose channels are interleaved into one.
    /// @param rowStrideBytes distance in bytes from one row to the next. It must be at least the
    ///        bytes of samples in a row and a whole number of samples.
    /// @return `GeometryError::None` when a view can be built; otherwise the rule that refused.
    [[nodiscard]] static GeometryError check(std::span<SampleT> samples, std::uint32_t width, std::uint32_t height,
                                             std::uint32_t samplesPerPixel, std::uint64_t rowStrideBytes) noexcept
    {
        const std::optional<ImageLayout> layout = planeLayout(width, height, samplesPerPixel, rowStrideBytes);
        if (!layout.has_value())
        {
            return planeGeometryError(width, height, samplesPerPixel, rowStrideBytes);
        }
        return checkBuffer(samples, *layout);
    }

    /// A view of a plane, where the buffer and the geometry describe one.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param samples the buffer the plane lives in, which must outlive every view of it.
    /// @param width the plane's width in pixels.
    /// @param height the plane's height in pixels.
    /// @param samplesPerPixel how many of one pixel's samples this plane holds.
    /// @param rowStrideBytes distance in bytes from one row to the next.
    /// @return the view, or an empty optional exactly when `check` would name a reason.
    [[nodiscard]] static std::optional<PlaneView> create(std::span<SampleT> samples, std::uint32_t width,
                                                         std::uint32_t height, std::uint32_t samplesPerPixel,
                                                         std::uint64_t rowStrideBytes) noexcept
    {
        const std::optional<ImageLayout> layout = planeLayout(width, height, samplesPerPixel, rowStrideBytes);
        if (!layout.has_value() || checkBuffer(samples, *layout) != GeometryError::None)
        {
            return std::nullopt;
        }
        return PlaneView{samples.data(), *layout};
    }

    /// Adopts a writable plane as a read-only one.
    ///
    /// Implicit, and available only in this direction: `PlaneView<const float>` is constructible
    /// from `PlaneView<float>`, and no overload turns a read-only view back into a writable one.
    /// That is what lets a function declare read-only access in its signature and get it.
    ///
    /// @tparam OtherT the writable sample type, which must be this view's type without its const.
    /// @param writable the view to borrow from. Nothing is copied but the pointer and the geometry,
    ///        both already checked.
    template <typename OtherT>
        requires(std::is_const_v<SampleT> && std::is_same_v<std::remove_const_t<SampleT>, OtherT>)
    constexpr PlaneView(const PlaneView<OtherT>& writable) noexcept
        : m_data(writable.data()), m_layout(writable.layout())
    {
    }

    /// This plane as a read-only view of the same samples.
    ///
    /// The named form of the conversion above, for a call site where the target type is deduced and
    /// the implicit conversion therefore does not happen.
    ///
    /// @return a view of the same memory through which nothing can be written.
    [[nodiscard]] constexpr PlaneView<const std::remove_const_t<SampleT>> constView() const noexcept
    {
        return PlaneView<const std::remove_const_t<SampleT>>::createChecked(m_data, m_layout);
    }

    /// Where the plane's first sample is.
    ///
    /// @return the base address, never null, aligned for a sample of this type.
    [[nodiscard]] constexpr SampleT* data() const noexcept
    {
        return m_data;
    }

    /// Where the plane's samples sit, as the validated layout that was derived for it.
    ///
    /// Its plane count is one, its element size is `sizeof(SampleT)`, and its channel count is this
    /// plane's samples per pixel.
    ///
    /// @return the layout.
    [[nodiscard]] constexpr const ImageLayout& layout() const noexcept
    {
        return m_layout;
    }

    /// The plane's dimensions.
    ///
    /// @return the extent, whose channel count is `samplesPerPixel()`.
    [[nodiscard]] constexpr const ImageExtent& extent() const noexcept
    {
        return m_layout.extent();
    }

    /// How many pixels wide the plane is.
    ///
    /// @return the width, never zero.
    [[nodiscard]] constexpr std::uint32_t width() const noexcept
    {
        return m_layout.extent().width();
    }

    /// How many rows the plane has.
    ///
    /// @return the height, never zero.
    [[nodiscard]] constexpr std::uint32_t height() const noexcept
    {
        return m_layout.extent().height();
    }

    /// How many of one pixel's samples this plane holds.
    ///
    /// @return the samples per pixel, never zero.
    [[nodiscard]] constexpr std::uint32_t samplesPerPixel() const noexcept
    {
        return m_layout.samplesPerPixel();
    }

    /// Distance in bytes from one row to the next.
    ///
    /// @return the stride exactly as it was stated, never normalised.
    [[nodiscard]] constexpr std::uint64_t rowStrideBytes() const noexcept
    {
        return m_layout.rowStrideBytes();
    }

    /// How many samples one row holds, without the padding after them.
    ///
    /// @return width times samples per pixel.
    [[nodiscard]] constexpr std::uint64_t rowSampleCount() const noexcept
    {
        return static_cast<std::uint64_t>(width()) * static_cast<std::uint64_t>(samplesPerPixel());
    }

    /// How many samples the plane holds.
    ///
    /// @return width times height times samples per pixel, which is what a full pass visits.
    [[nodiscard]] constexpr std::uint64_t sampleCount() const noexcept
    {
        return rowSampleCount() * static_cast<std::uint64_t>(height());
    }

    /// Whether the rows follow one another with no padding at all.
    ///
    /// The question to ask before treating the whole plane as one run instead of walking it row by
    /// row. It is a proof rather than a hint: when it is true, `contiguousSpan` returns the run.
    ///
    /// @return true when the row stride is exactly the bytes of samples in a row.
    [[nodiscard]] constexpr bool isContiguous() const noexcept
    {
        return m_layout.rowStrideBytes() == m_layout.minimumRowBytes();
    }

    /// The samples of one row.
    ///
    /// The call a kernel makes once per row. What comes back covers the row's own samples and stops
    /// before whatever padding follows them, so writing to its end cannot reach the next row.
    ///
    /// @param y the row, below `height()`.
    /// @return a span of `rowSampleCount()` samples, or an empty optional when `y` is out of range.
    [[nodiscard]] std::optional<std::span<SampleT>> row(std::uint32_t y) const noexcept
    {
        if (y >= height())
        {
            return std::nullopt;
        }
        return std::span<SampleT>{m_data + static_cast<std::uint64_t>(y) * rowElementStride(),
                                  static_cast<std::size_t>(rowSampleCount())};
    }

    /// The whole plane as one run, when it is one.
    ///
    /// @return a span of `sampleCount()` samples when `isContiguous()`, and an empty optional
    ///         otherwise. It is empty rather than a best effort on purpose: a caller that treats a
    ///         padded plane as a run reads the padding as samples and writes samples into it.
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
    /// sample pays for a bounds check on every one of them.
    ///
    /// @param x the column, below `width()`.
    /// @param y the row, below `height()`.
    /// @param sampleInPixel which of the pixel's samples, below `samplesPerPixel()`.
    /// @return the sample's address, or null when any index is out of range.
    [[nodiscard]] SampleT* sampleAt(std::uint32_t x, std::uint32_t y, std::uint32_t sampleInPixel = 0u) const noexcept
    {
        if (x >= width() || y >= height() || sampleInPixel >= samplesPerPixel())
        {
            return nullptr;
        }
        return m_data + static_cast<std::uint64_t>(y) * rowElementStride() +
               (static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(samplesPerPixel()) +
                static_cast<std::uint64_t>(sampleInPixel));
    }

    /// Whether a region of this plane can be viewed on its own, and if not, why not.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param originX the region's first column, in pixels from the left.
    /// @param originY the region's first row.
    /// @param regionWidth the region's width in pixels.
    /// @param regionHeight the region's height in pixels.
    /// @return `GeometryError::None` when a view can be built; `RegionOutOfBounds` when the region
    ///         is not wholly inside this plane; otherwise the rule that refused.
    [[nodiscard]] GeometryError checkSubplane(std::uint32_t originX, std::uint32_t originY, std::uint32_t regionWidth,
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
        return planeGeometryError(regionWidth, regionHeight, samplesPerPixel(), rowStrideBytes());
    }

    /// A view of a region of this plane, sharing its memory.
    ///
    /// The row stride is kept, which is what makes a subplane free: the samples do not move, and
    /// only the first address and the extent change. A region that is not the full width is
    /// therefore padded even when the plane it came from was contiguous, and it reports so.
    ///
    /// @param originX the region's first column, in pixels from the left.
    /// @param originY the region's first row.
    /// @param regionWidth the region's width in pixels.
    /// @param regionHeight the region's height in pixels.
    /// @return the view, or an empty optional exactly when `checkSubplane` would name a reason.
    [[nodiscard]] std::optional<PlaneView> subplane(std::uint32_t originX, std::uint32_t originY,
                                                    std::uint32_t regionWidth,
                                                    std::uint32_t regionHeight) const noexcept
    {
        if (checkSubplane(originX, originY, regionWidth, regionHeight) != GeometryError::None)
        {
            return std::nullopt;
        }
        const std::optional<ImageLayout> layout =
            planeLayout(regionWidth, regionHeight, samplesPerPixel(), rowStrideBytes());
        if (!layout.has_value())
        {
            return std::nullopt;
        }
        SampleT* const origin = m_data + static_cast<std::uint64_t>(originY) * rowElementStride() +
                                static_cast<std::uint64_t>(originX) * static_cast<std::uint64_t>(samplesPerPixel());
        return PlaneView{origin, *layout};
    }

    /// Two plane views are equal when they describe the same samples in the same place.
    ///
    /// Identity rather than content: two views of different buffers holding equal samples are not
    /// equal, and are not meant to be.
    friend constexpr bool operator==(const PlaneView& left, const PlaneView& right) noexcept = default;

    /// Builds a view whose geometry has already been proved.
    ///
    /// Public because one instantiation of this template cannot reach another's private members,
    /// and both `ImageView` and the const conversion have to hand over a pair that is already
    /// checked. It is not the entry point for a caller holding a raw pointer: `create` is, and it
    /// checks.
    ///
    /// @param data the base address, already proved non-null and aligned for this type.
    /// @param layout the plane's layout, already proved to describe one plane of this element size
    ///        within a buffer that holds it.
    /// @return the view.
    [[nodiscard]] static constexpr PlaneView createChecked(SampleT* data, const ImageLayout& layout) noexcept
    {
        return PlaneView{data, layout};
    }

private:
    /// Builds the value once the buffer and the geometry have been checked.
    ///
    /// @param data the checked base address.
    /// @param layout the checked layout.
    constexpr PlaneView(SampleT* data, const ImageLayout& layout) noexcept : m_data(data), m_layout(layout) {}

    /// How many samples one row is from the next.
    ///
    /// The stride is proved to be a whole number of samples at construction, so this division is
    /// exact and the walk can stay in the sample type rather than casting to bytes and back - which
    /// is the cast that would have to drop the constness of a read-only view and put it back.
    ///
    /// @return the row stride in samples.
    [[nodiscard]] constexpr std::uint64_t rowElementStride() const noexcept
    {
        return m_layout.rowStrideBytes() / sizeof(SampleT);
    }

    /// The layout a plane of these numbers has.
    ///
    /// @param width the plane's width in pixels.
    /// @param height the plane's height in pixels.
    /// @param samplesPerPixel how many of a pixel's samples the plane holds.
    /// @param rowStrideBytes distance in bytes from one row to the next.
    /// @return the layout, or an empty optional when the numbers do not describe one.
    [[nodiscard]] static std::optional<ImageLayout> planeLayout(std::uint32_t width, std::uint32_t height,
                                                                std::uint32_t samplesPerPixel,
                                                                std::uint64_t rowStrideBytes) noexcept
    {
        const EngineLimits limits = viewGeometryLimits(width, height, samplesPerPixel);
        const std::optional<ImageExtent> extent = ImageExtent::create(width, height, samplesPerPixel, limits);
        if (!extent.has_value())
        {
            return std::nullopt;
        }
        const ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = static_cast<std::uint32_t>(sizeof(SampleT)),
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
        return ImageLayout::createWithStrides(request, rowStrideBytes, planeStrideOf(request, rowStrideBytes), limits);
    }

    /// Why a plane of these numbers has no layout, or `None` when it has one.
    ///
    /// It runs the same derivation as `planeLayout` and asks for the reason instead of the value,
    /// so the two cannot disagree about which rule refused.
    ///
    /// @param width the plane's width in pixels.
    /// @param height the plane's height in pixels.
    /// @param samplesPerPixel how many of a pixel's samples the plane holds.
    /// @param rowStrideBytes distance in bytes from one row to the next.
    /// @return the rule that refused, or `GeometryError::None`.
    [[nodiscard]] static GeometryError planeGeometryError(std::uint32_t width, std::uint32_t height,
                                                          std::uint32_t samplesPerPixel,
                                                          std::uint64_t rowStrideBytes) noexcept
    {
        const EngineLimits limits = viewGeometryLimits(width, height, samplesPerPixel);
        const GeometryError extentError = ImageExtent::check(width, height, samplesPerPixel, limits);
        if (extentError != GeometryError::None)
        {
            return extentError;
        }
        const std::optional<ImageExtent> extent = ImageExtent::create(width, height, samplesPerPixel, limits);
        if (!extent.has_value())
        {
            return GeometryError::ArithmeticOverflow;
        }
        const ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = static_cast<std::uint32_t>(sizeof(SampleT)),
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
        return ImageLayout::checkWithStrides(request, rowStrideBytes, planeStrideOf(request, rowStrideBytes), limits);
    }

    /// The plane stride a single-plane layout is built with.
    ///
    /// A one-plane layout has no distance between planes to describe, so this is the smallest value
    /// the layout's own rules accept: it drops straight back out of every offset the layout then
    /// computes, and stating a larger one would claim bytes past the last row that nothing owns.
    ///
    /// @param request the request whose extent and element size the stride must cover.
    /// @param rowStrideBytes distance in bytes from one row to the next.
    /// @return the plane stride, saturated rather than wrapped when the product does not fit, so
    ///         that the layout refuses the geometry instead of accepting a wrapped stride.
    [[nodiscard]] static constexpr std::uint64_t planeStrideOf(const ImageLayoutRequest& request,
                                                               std::uint64_t rowStrideBytes) noexcept
    {
        const CheckedSize rows = CheckedSize{rowStrideBytes} * CheckedSize{request.extent.height() - 1u};
        const CheckedSize total = rows + CheckedSize{request.extent.width()} * CheckedSize{request.samplesPerPixel} *
                                             CheckedSize{request.elementSizeBytes};
        return total.valueOr((std::numeric_limits<std::uint64_t>::max)());
    }

    /// Whether a buffer holds what a layout describes, and begins where a sample of this type may
    /// be read from.
    ///
    /// The size is asked first: an absent buffer is a buffer too small, and reporting its null
    /// address as a misalignment would name the wrong problem.
    ///
    /// @param samples the buffer.
    /// @param layout the geometry to be written through it.
    /// @return `GeometryError::None`, or the rule that refused.
    [[nodiscard]] static GeometryError checkBuffer(std::span<SampleT> samples, const ImageLayout& layout) noexcept
    {
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

    /// Where the plane's first sample is. Never null.
    SampleT* m_data;

    /// The plane's checked geometry: one plane, this element size, the stated row stride.
    ImageLayout m_layout;
};

/// Whether two plane views share a byte.
///
/// @tparam LeftT the first view's sample type.
/// @tparam RightT the second view's sample type.
/// @param left the first view.
/// @param right the second view.
/// @return true when the two planes share at least one byte of memory, on the conservative terms
///         `regionsOverlap` describes. Two views of the same plane report true, which is what makes
///         this usable as an in-place check as well as an aliasing one.
template <ViewableSample LeftT, ViewableSample RightT>
[[nodiscard]] inline bool viewsOverlap(const PlaneView<LeftT>& left, const PlaneView<RightT>& right) noexcept
{
    return regionsOverlap(left.data(), left.layout().totalBytes(), right.data(), right.layout().totalBytes());
}

} // namespace an

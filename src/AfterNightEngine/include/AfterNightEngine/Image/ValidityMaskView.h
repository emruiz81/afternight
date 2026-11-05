// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One bit per pixel, borrowed rather than owned, checked once.
///
/// This is the materialised half of `Image/ValidityDescriptor.h`: the description says a mask
/// decides, and this is the mask. One bit per pixel per plane, packed eight to a byte, least
/// significant bit first, with a stated distance from one row to the next and from one plane to the
/// next - the same shape `Image/ImageView.h` gives samples, for the same reason. Once the strides
/// and the buffer's size have been checked together, walking the mask needs no further validation.
///
/// **A set bit means the sample means something.** Stated because the opposite convention exists and
/// both are defensible; what is not defensible is two parts of one engine disagreeing. Set is valid,
/// everywhere, and the zeroed allocation a mask starts from therefore means "nothing yet", which is
/// the safe direction: a mask nobody filled in selects nothing rather than selecting everything.
///
/// **The bits after the last pixel of a row are zero, always.** A row of 10 pixels occupies two
/// bytes and six of the second byte's bits belong to no pixel. Every operation here writes them as
/// zero rather than leaving them alone, which is the opposite of what `ImageStorage` does with the
/// padding between rows - and the difference is that these bits share a byte with real ones. Left
/// undefined, they would make a popcount wrong, a byte-for-byte comparison of two equal masks wrong,
/// and a digest of a fixture depend on the allocator's history. `clearPadBits` is how a view built
/// over bytes from somewhere else is brought into that state.
///
/// **A plane may be shared.** A mask whose plane count is one covers every channel of the image it
/// is paired with; a mask whose plane count is the image's covers each channel separately. Both are
/// real - a footprint is shared and a per-channel rejection is not - and which one a mask is, is
/// read off its own extent rather than inferred from the image.
///
/// There is no subview. A region of a mask starting at a column that is not a multiple of eight is a
/// bit-shift of every byte rather than a pointer and a stride, and an operation that needs one is
/// doing geometry. That belongs with the geometry work and not in the type that packs the bits.

#pragma once

#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace an
{

/// The byte a packed validity mask is read and written through.
///
/// Exactly `std::uint8_t` and its const form, and nothing else. A mask is bits rather than samples,
/// so the eight resident representations have nothing to do with it, and admitting any integer type
/// here would make the packing depend on which one a caller happened to pass.
///
/// @tparam ByteT the type to test.
template <typename ByteT>
concept ValidityMaskByte = std::is_same_v<std::remove_const_t<ByteT>, std::uint8_t>;

/// How many bytes one row of this many pixels packs into.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param width how many pixels one row holds.
/// @return the bytes, rounded up, so a row of 10 pixels reports 2.
[[nodiscard]] constexpr std::uint64_t packedMaskRowBytes(std::uint32_t width) noexcept
{
    return (static_cast<std::uint64_t>(width) + 7u) / 8u;
}

/// The bits of a row's last byte that belong to a pixel.
///
/// The mask a write applies to the final byte of a row so that the bits past the last pixel stay
/// zero. A width that is a multiple of eight has no such bits and gets `0xFF`.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param width how many pixels one row holds.
/// @return a byte whose set bits are the ones a pixel of this row occupies in its last byte.
[[nodiscard]] constexpr std::uint8_t packedMaskLastByteMask(std::uint32_t width) noexcept
{
    const std::uint32_t remainder = width % 8u;
    return remainder == 0u ? static_cast<std::uint8_t>(0xFFu) : static_cast<std::uint8_t>((1u << remainder) - 1u);
}

/// One bit per pixel, borrowed from whoever owns the memory.
///
/// There is no way to build one that has not been checked: the constructor is private and the
/// static entry points below are the only way in. So a function taking a `ValidityMaskView` may
/// address any bit its extent names without re-deriving whether the buffer holds it.
///
/// Constness lives in the byte type, as it does in `std::span` and in the sample views:
/// `ValidityMaskView<const std::uint8_t>` converts from `ValidityMaskView<std::uint8_t>` and never
/// the other way, and only the writable one has the entry points that write.
///
/// Trivially copyable, and deliberately without a default constructor, for the reason the sample
/// views have none: a mask of no pixels is not a thing, and a null-pointer view would put a branch
/// in every entry point below. A member assigned later is an `std::optional<ValidityMaskView>`.
///
/// @tparam ByteT `std::uint8_t`, or the const-qualified form of it for a read-only view.
template <ValidityMaskByte ByteT> class ValidityMaskView
{
public:
    /// The type of one packed byte, const-qualified exactly when this view is read-only.
    using Byte = ByteT;

    /// Whether a buffer and a geometry describe a packed mask, and if not, why not.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param bits the buffer the mask lives in. It must hold every byte the geometry names,
    ///        counting the padding between rows and planes but not any padding after the last row.
    /// @param extent the mask's dimensions. Its channel count is how many bit-planes the mask has:
    ///        one for a mask shared by every channel, the image's channel count for a mask that
    ///        covers each channel separately.
    /// @param rowStrideBytes distance in bytes from one row to the next. At least
    ///        `packedMaskRowBytes(extent.width())`.
    /// @param planeStrideBytes distance in bytes from one bit-plane to the next. At least the
    ///        bytes one whole plane of rows occupies.
    /// @return `GeometryError::None` when a view can be built; `StrideTooSmall` when a stride does
    ///         not cover what it is meant to; `ArithmeticOverflow` when the geometry's byte count
    ///         does not exist; `BufferTooSmall` when the buffer does not hold it; or
    ///         `BaseAddressMisaligned` for a null buffer.
    [[nodiscard]] static GeometryError check(std::span<ByteT> bits, const ImageExtent& extent,
                                             std::uint64_t rowStrideBytes, std::uint64_t planeStrideBytes) noexcept
    {
        const std::uint64_t minimumRow = packedMaskRowBytes(extent.width());
        if (rowStrideBytes < minimumRow)
        {
            return GeometryError::StrideTooSmall;
        }
        const CheckedSize planeBytes = CheckedSize{rowStrideBytes} * CheckedSize{extent.height()};
        if (!planeBytes.isValid())
        {
            return GeometryError::ArithmeticOverflow;
        }
        if (planeStrideBytes < *planeBytes.value())
        {
            return GeometryError::StrideTooSmall;
        }
        const CheckedSize total = spannedBytes(extent, rowStrideBytes, planeStrideBytes);
        if (!total.isValid())
        {
            return GeometryError::ArithmeticOverflow;
        }
        if (static_cast<std::uint64_t>(bits.size()) < *total.value())
        {
            return GeometryError::BufferTooSmall;
        }
        if (bits.data() == nullptr)
        {
            return GeometryError::BaseAddressMisaligned;
        }
        return GeometryError::None;
    }

    /// A view of a packed mask, where the buffer and the geometry describe one.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param bits the buffer the mask lives in, which must outlive every view of it.
    /// @param extent the mask's dimensions, whose channel count is its bit-plane count.
    /// @param rowStrideBytes distance in bytes from one row to the next.
    /// @param planeStrideBytes distance in bytes from one bit-plane to the next.
    /// @return the view, or an empty optional exactly when `check` would name a reason.
    [[nodiscard]] static std::optional<ValidityMaskView> create(std::span<ByteT> bits, const ImageExtent& extent,
                                                                std::uint64_t rowStrideBytes,
                                                                std::uint64_t planeStrideBytes) noexcept
    {
        if (check(bits, extent, rowStrideBytes, planeStrideBytes) != GeometryError::None)
        {
            return std::nullopt;
        }
        return ValidityMaskView{bits.data(), extent, rowStrideBytes, planeStrideBytes};
    }

    /// A view of a mask packed as tightly as this geometry allows.
    ///
    /// The ordinary case, and the shape an owned mask has: rows follow one another with no padding
    /// and planes follow rows.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param bits the buffer the mask lives in, which must outlive every view of it.
    /// @param extent the mask's dimensions, whose channel count is its bit-plane count.
    /// @return the view, or an empty optional when the buffer cannot hold the geometry.
    [[nodiscard]] static std::optional<ValidityMaskView> createTight(std::span<ByteT> bits,
                                                                     const ImageExtent& extent) noexcept
    {
        const std::uint64_t rowStride = packedMaskRowBytes(extent.width());
        const CheckedSize planeStride = CheckedSize{rowStride} * CheckedSize{extent.height()};
        if (!planeStride.isValid())
        {
            return std::nullopt;
        }
        return create(bits, extent, rowStride, *planeStride.value());
    }

    /// Adopts a writable mask as a read-only one.
    ///
    /// Implicit, and available only in this direction, exactly as the sample views' conversion is.
    ///
    /// @tparam OtherT the writable byte type, which must be this view's type without its const.
    /// @param writable the view to borrow from.
    template <typename OtherT>
        requires(std::is_const_v<ByteT> && std::is_same_v<std::remove_const_t<ByteT>, OtherT>)
    constexpr ValidityMaskView(const ValidityMaskView<OtherT>& writable) noexcept
        : m_bits(writable.data()), m_extent(writable.extent()), m_rowStrideBytes(writable.rowStrideBytes()),
          m_planeStrideBytes(writable.planeStrideBytes())
    {
    }

    /// This mask as a read-only view of the same bits.
    ///
    /// The named form of the conversion above, for a call site where the target type is deduced and
    /// the implicit conversion therefore does not happen.
    ///
    /// @return a view of the same memory through which nothing can be written.
    [[nodiscard]] constexpr ValidityMaskView<const std::uint8_t> constView() const noexcept
    {
        return ValidityMaskView<const std::uint8_t>::createChecked(m_bits, m_extent, m_rowStrideBytes,
                                                                   m_planeStrideBytes);
    }

    /// Where the mask's first byte is.
    ///
    /// @return the base address, never null.
    [[nodiscard]] constexpr ByteT* data() const noexcept
    {
        return m_bits;
    }

    /// The mask's dimensions.
    ///
    /// @return the extent, whose channel count is `planeCount()`.
    [[nodiscard]] constexpr const ImageExtent& extent() const noexcept
    {
        return m_extent;
    }

    /// How many pixels wide the mask is.
    ///
    /// @return the width, never zero.
    [[nodiscard]] constexpr std::uint32_t width() const noexcept
    {
        return m_extent.width();
    }

    /// How many rows the mask has.
    ///
    /// @return the height, never zero.
    [[nodiscard]] constexpr std::uint32_t height() const noexcept
    {
        return m_extent.height();
    }

    /// How many bit-planes the mask has.
    ///
    /// @return one for a mask shared by every channel; the image's channel count for a mask that
    ///         covers each channel separately.
    [[nodiscard]] constexpr std::uint32_t planeCount() const noexcept
    {
        return m_extent.channelCount();
    }

    /// Whether this mask states one answer for every channel of the image it covers.
    ///
    /// @return true when there is exactly one bit-plane.
    [[nodiscard]] constexpr bool isShared() const noexcept
    {
        return planeCount() == 1u;
    }

    /// Distance in bytes from one row to the next.
    ///
    /// @return the stride exactly as it was stated, never normalised.
    [[nodiscard]] constexpr std::uint64_t rowStrideBytes() const noexcept
    {
        return m_rowStrideBytes;
    }

    /// Distance in bytes from one bit-plane to the next.
    ///
    /// @return the stride exactly as it was stated, never normalised.
    [[nodiscard]] constexpr std::uint64_t planeStrideBytes() const noexcept
    {
        return m_planeStrideBytes;
    }

    /// How many bytes one row's own bits occupy, without the padding after them.
    ///
    /// @return the width rounded up to a whole number of bytes.
    [[nodiscard]] constexpr std::uint64_t minimumRowBytes() const noexcept
    {
        return packedMaskRowBytes(width());
    }

    /// How many pixels this mask answers for.
    ///
    /// @return width times height times plane count.
    [[nodiscard]] constexpr std::uint64_t bitCount() const noexcept
    {
        return m_extent.sampleCount();
    }

    /// How many bytes the mask spans, from its first to its last.
    ///
    /// @return the byte count, counting the padding between rows and planes but not any padding
    ///         after the last row.
    [[nodiscard]] constexpr std::uint64_t spannedByteCount() const noexcept
    {
        return spannedBytes(m_extent, m_rowStrideBytes, m_planeStrideBytes).valueOr(0u);
    }

    /// Whether the rows and planes follow one another with no padding at all.
    ///
    /// @return true when both strides are the tightest the geometry allows. A width that is not a
    ///         multiple of eight still has pad bits in its last byte; those are part of the packing
    ///         rather than padding between rows, and they do not make a mask non-contiguous.
    [[nodiscard]] constexpr bool isContiguous() const noexcept
    {
        return m_rowStrideBytes == minimumRowBytes() &&
               m_planeStrideBytes == m_rowStrideBytes * static_cast<std::uint64_t>(height());
    }

    /// The bytes of one row of one bit-plane.
    ///
    /// The call a kernel makes once per row. What comes back covers the row's own bytes and stops
    /// before whatever padding follows them, so writing to its end cannot reach the next row - but
    /// its last byte may still hold bits past the last pixel, which the packing says are zero.
    ///
    /// @param planeIndex the bit-plane, below `planeCount()`.
    /// @param y the row, below `height()`.
    /// @return a span of `minimumRowBytes()` bytes, or an empty optional when either index is out
    ///         of range.
    [[nodiscard]] std::optional<std::span<ByteT>> row(std::uint32_t planeIndex, std::uint32_t y) const noexcept
    {
        if (planeIndex >= planeCount() || y >= height())
        {
            return std::nullopt;
        }
        return std::span<ByteT>{m_bits + rowOffset(planeIndex, y), static_cast<std::size_t>(minimumRowBytes())};
    }

    /// Whether one pixel means something.
    ///
    /// For tests, parsers and cold code. A kernel takes a row once and walks its bytes; calling
    /// this per pixel pays for three bounds tests and a division on every one of them.
    ///
    /// @param planeIndex the bit-plane, below `planeCount()`.
    /// @param x the column, below `width()`.
    /// @param y the row, below `height()`.
    /// @return whether the bit is set, or an empty optional when any index is out of range.
    [[nodiscard]] std::optional<bool> test(std::uint32_t planeIndex, std::uint32_t x, std::uint32_t y) const noexcept
    {
        if (planeIndex >= planeCount() || x >= width() || y >= height())
        {
            return std::nullopt;
        }
        const std::uint8_t byte = static_cast<std::uint8_t>(m_bits[rowOffset(planeIndex, y) + (x / 8u)]);
        return (byte & static_cast<std::uint8_t>(1u << (x % 8u))) != 0u;
    }

    /// States whether one pixel means something.
    ///
    /// @param planeIndex the bit-plane, below `planeCount()`.
    /// @param x the column, below `width()`.
    /// @param y the row, below `height()`.
    /// @param valid true when the sample means something.
    /// @return true when the bit was written; false when any index is out of range, in which case
    ///         nothing was written.
    [[nodiscard]] bool setValid(std::uint32_t planeIndex, std::uint32_t x, std::uint32_t y, bool valid) const noexcept
        requires(!std::is_const_v<ByteT>)
    {
        if (planeIndex >= planeCount() || x >= width() || y >= height())
        {
            return false;
        }
        std::uint8_t& byte = m_bits[rowOffset(planeIndex, y) + (x / 8u)];
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << (x % 8u));
        byte = valid ? static_cast<std::uint8_t>(byte | bit) : static_cast<std::uint8_t>(byte & ~bit);
        return true;
    }

    /// States the same answer for every pixel of the mask.
    ///
    /// The bits past the last pixel of each row are written as zero whichever answer is filled in,
    /// which is what keeps a filled mask and a hand-written one byte-for-byte comparable.
    ///
    /// @param valid true when every sample means something.
    void fill(bool valid) const noexcept
        requires(!std::is_const_v<ByteT>)
    {
        const std::uint8_t filled = valid ? static_cast<std::uint8_t>(0xFFu) : static_cast<std::uint8_t>(0x00u);
        const std::uint8_t last = packedMaskLastByteMask(width());
        for (std::uint32_t planeIndex = 0u; planeIndex < planeCount(); ++planeIndex)
        {
            for (std::uint32_t y = 0u; y < height(); ++y)
            {
                const std::optional<std::span<ByteT>> bytes = row(planeIndex, y);
                for (std::uint8_t& byte : *bytes)
                {
                    byte = filled;
                }
                (*bytes).back() = static_cast<std::uint8_t>((*bytes).back() & last);
            }
        }
    }

    /// Writes the bits past the last pixel of every row as zero.
    ///
    /// What a view built over bytes from somewhere else calls before it is counted or compared. A
    /// mask this engine allocated is already in this state and stays in it.
    void clearPadBits() const noexcept
        requires(!std::is_const_v<ByteT>)
    {
        const std::uint8_t last = packedMaskLastByteMask(width());
        for (std::uint32_t planeIndex = 0u; planeIndex < planeCount(); ++planeIndex)
        {
            for (std::uint32_t y = 0u; y < height(); ++y)
            {
                const std::optional<std::span<ByteT>> bytes = row(planeIndex, y);
                (*bytes).back() = static_cast<std::uint8_t>((*bytes).back() & last);
            }
        }
    }

    /// How many of the pixels this mask answers for mean something.
    ///
    /// The bits past the last pixel of a row are excluded whatever they hold, so a view over bytes
    /// nobody cleaned still counts the right number.
    ///
    /// The total is `bitCount()`, and the two are separate calls rather than one pair because this
    /// one walks the whole mask and that one is arithmetic. `countValidity` in
    /// `Image/ValidityMask.h` is the pair, for a caller that wants both.
    ///
    /// @return how many bits are set.
    [[nodiscard]] std::uint64_t countValid() const noexcept
    {
        const std::uint8_t last = packedMaskLastByteMask(width());
        std::uint64_t set = 0u;
        for (std::uint32_t planeIndex = 0u; planeIndex < planeCount(); ++planeIndex)
        {
            for (std::uint32_t y = 0u; y < height(); ++y)
            {
                const std::optional<std::span<ByteT>> bytes = row(planeIndex, y);
                const std::size_t byteCount = (*bytes).size();
                for (std::size_t index = 0u; index + 1u < byteCount; ++index)
                {
                    set += static_cast<std::uint64_t>(std::popcount(static_cast<std::uint8_t>((*bytes)[index])));
                }
                set += static_cast<std::uint64_t>(
                    std::popcount(static_cast<std::uint8_t>((*bytes)[byteCount - 1u] & last)));
            }
        }
        return set;
    }

    /// Two mask views are equal when they describe the same bits in the same place.
    ///
    /// Identity rather than content: two views of different buffers holding equal bits are not
    /// equal, and are not meant to be.
    friend constexpr bool operator==(const ValidityMaskView& left, const ValidityMaskView& right) noexcept = default;

    /// Builds a view whose buffer and geometry have already been proved to go together.
    ///
    /// Public because one instantiation of this template cannot reach another's private members,
    /// and the const conversion has to hand over a set that is already checked, and because an
    /// owner that allocated the buffer for this exact geometry has proved everything `check` asks.
    /// It is not the entry point for a caller holding a raw pointer: `create` is, and it checks.
    ///
    /// @param bits the base address, already proved non-null.
    /// @param extent the mask's dimensions.
    /// @param rowStrideBytes distance in bytes from one row to the next, already proved sufficient.
    /// @param planeStrideBytes distance in bytes from one bit-plane to the next, already proved
    ///        sufficient.
    /// @return the view.
    [[nodiscard]] static constexpr ValidityMaskView createChecked(ByteT* bits, const ImageExtent& extent,
                                                                  std::uint64_t rowStrideBytes,
                                                                  std::uint64_t planeStrideBytes) noexcept
    {
        return ValidityMaskView{bits, extent, rowStrideBytes, planeStrideBytes};
    }

private:
    /// Builds the value once the buffer and the geometry have been checked.
    ///
    /// @param bits the checked base address.
    /// @param extent the mask's dimensions.
    /// @param rowStrideBytes distance in bytes from one row to the next.
    /// @param planeStrideBytes distance in bytes from one bit-plane to the next.
    constexpr ValidityMaskView(ByteT* bits, const ImageExtent& extent, std::uint64_t rowStrideBytes,
                               std::uint64_t planeStrideBytes) noexcept
        : m_bits(bits), m_extent(extent), m_rowStrideBytes(rowStrideBytes), m_planeStrideBytes(planeStrideBytes)
    {
    }

    /// Where one row's first byte sits, in bytes from the mask's first.
    ///
    /// @param planeIndex the bit-plane, already proved in range.
    /// @param y the row, already proved in range.
    /// @return the offset.
    [[nodiscard]] constexpr std::uint64_t rowOffset(std::uint32_t planeIndex, std::uint32_t y) const noexcept
    {
        return static_cast<std::uint64_t>(planeIndex) * m_planeStrideBytes +
               static_cast<std::uint64_t>(y) * m_rowStrideBytes;
    }

    /// How many bytes a mask of this geometry spans.
    ///
    /// The last plane's last row stops at its own bytes rather than at its stride, so a buffer sized
    /// to exactly this is enough and a view is not asked to own padding nobody allocated.
    ///
    /// @param extent the mask's dimensions.
    /// @param rowStrideBytes distance in bytes from one row to the next.
    /// @param planeStrideBytes distance in bytes from one bit-plane to the next.
    /// @return the byte count, or an overflowed size when it does not exist.
    [[nodiscard]] static constexpr CheckedSize spannedBytes(const ImageExtent& extent, std::uint64_t rowStrideBytes,
                                                            std::uint64_t planeStrideBytes) noexcept
    {
        const CheckedSize planes = CheckedSize{planeStrideBytes} * CheckedSize{extent.channelCount() - 1u};
        const CheckedSize rows = CheckedSize{rowStrideBytes} * CheckedSize{extent.height() - 1u};
        return planes + rows + CheckedSize{packedMaskRowBytes(extent.width())};
    }

    /// Where the mask's first byte is. Never null.
    ByteT* m_bits;

    /// The mask's dimensions, whose channel count is its bit-plane count.
    ImageExtent m_extent;

    /// Distance in bytes from one row to the next.
    std::uint64_t m_rowStrideBytes;

    /// Distance in bytes from one bit-plane to the next.
    std::uint64_t m_planeStrideBytes;
};

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where an image's samples sit in memory, checked before a pointer exists.
///
/// An extent says how many samples there are. A layout says where they are: how many bytes a sample
/// takes, how many of a pixel's samples share a plane, how many planes there are, and the distance
/// from one row or plane to the next. Those distances are the numbers that turn a plausible
/// descriptor into a wrong pointer - a row stride one element short reads its neighbour's first
/// sample, and a total that wrapped sizes a buffer smaller than the geometry written through it - so
/// every one of them is validated here, before anything computes an address from it.
///
/// The arrangement is stated as samples-per-plane rather than as an enumeration of planar,
/// interleaved and the rest. One plane per channel and one plane holding every channel are the same
/// arithmetic with different numbers, and this layer has no business knowing which of them a caller
/// calls what: naming the arrangement is the job of the descriptor that also carries what the
/// channels mean.

#pragma once

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>

#include <cstdint>
#include <optional>

namespace an
{

/// What a caller asks a layout for, before any of it has been checked.
///
/// A plain aggregate so that a call site can name each field - `{.extent = ..., .elementSizeBytes =
/// 4, ...}` - rather than pass five numbers positionally and get two of them the wrong way round.
/// Every field is required: the extent has no default, and a numeric field left out is zero, which
/// is refused rather than defaulted, because guessing an element size or a plane count is exactly
/// the kind of help that produces a descriptor nobody intended.
struct ImageLayoutRequest
{
    /// The image's pixel dimensions, already validated.
    ImageExtent extent;

    /// Bytes in one sample, which must be a power of two. Not a sample type: what the bytes mean
    /// belongs to the descriptor, and what they cost belongs here. An element is one scalar sample
    /// - three bytes of packed colour are three samples of one byte, not one sample of three.
    std::uint32_t elementSizeBytes;

    /// How many of one pixel's samples live in the same plane. One for an image stored a plane per
    /// channel; the channel count for one stored with the channels interleaved.
    std::uint32_t samplesPerPixel;

    /// How many planes the image is stored in. `samplesPerPixel * planeCount` has to be exactly the
    /// extent's channel count.
    std::uint32_t planeCount;

    /// The boundary every row and plane must start on, in bytes. Must be a power of two; use 1 for
    /// no alignment requirement, or `kDefaultBufferAlignmentBytes` for the engine's default.
    std::uint64_t rowAlignmentBytes;
};

/// Where an image's samples sit in memory, validated against the configured limits.
///
/// There is no way to build one that has not been checked: the constructor is private, and the four
/// static entry points below are the only way in. A function taking an `ImageLayout` can therefore
/// compute offsets from it without re-deriving whether they fit, because construction proved that
/// the total byte count exists, is addressable on this platform, and is within the configured
/// maximum.
///
/// Strides are kept exactly as they were stated or derived, never normalised: a layout that
/// describes a buffer somebody else allocated has to keep describing that buffer.
///
/// Trivially copyable and usable in a constant expression.
class ImageLayout
{
public:
    /// Whether a layout with derived strides can be built, and if not, why not.
    ///
    /// The derived row stride is the smallest one the requested alignment allows, and the derived
    /// plane stride is a whole image of those rows. That plane stride is deliberately not the
    /// tightest one possible: ending a plane at its last sample would start the next plane off the
    /// alignment boundary every row of it was padded to reach.
    ///
    /// A derived stride is never one this type would then refuse. Both the element size and the
    /// alignment are powers of two, so rounding a whole number of elements up to the alignment
    /// leaves a whole number of elements.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the extent, sample size, arrangement and alignment to build from.
    /// @param limits the configured maxima to check against, including the extent, which is
    ///        re-checked here in case it was validated against different limits.
    /// @return `GeometryError::None` when a layout can be built; otherwise the first rule that
    ///         refused, in the order `GeometryError` describes for a layout.
    [[nodiscard]] static constexpr GeometryError check(const ImageLayoutRequest& request,
                                                       const EngineLimits& limits) noexcept
    {
        const GeometryError arrangement = checkArrangement(request);
        if (arrangement != GeometryError::None)
        {
            return arrangement;
        }
        const CheckedSize rowStride = derivedRowStride(request);
        const CheckedSize planeStride = derivedPlaneStride(request, rowStride);
        if (!planeStride.isValid())
        {
            return GeometryError::ArithmeticOverflow;
        }
        return checkWithStrides(request, rowStride.valueOr(0u), planeStride.valueOr(0u), limits);
    }

    /// A layout with derived strides, where the request describes one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the extent, sample size, arrangement and alignment to build from.
    /// @param limits the configured maxima to check against.
    /// @return the layout, or an empty optional exactly when `check` would name a reason. Nothing
    ///         is allocated and nothing is mutated on refusal.
    [[nodiscard]] static constexpr std::optional<ImageLayout> create(const ImageLayoutRequest& request,
                                                                     const EngineLimits& limits) noexcept
    {
        if (check(request, limits) != GeometryError::None)
        {
            return std::nullopt;
        }
        const CheckedSize rowStride = derivedRowStride(request);
        return ImageLayout{request, rowStride.valueOr(0u), derivedPlaneStride(request, rowStride).valueOr(0u)};
    }

    /// Whether a layout with these stated strides can be built, and if not, why not.
    ///
    /// This is the entry point for a buffer the engine did not allocate - a decoder's, a mapped
    /// file's, another library's - where the strides are facts rather than choices. They are
    /// checked, not adjusted: a stride below the samples it must hold, one that is not a whole
    /// number of elements, or one that breaks the requested alignment is refused.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the extent, sample size, arrangement and alignment the strides must satisfy.
    /// @param rowStrideBytes distance in bytes from one row to the next within a plane.
    /// @param planeStrideBytes distance in bytes from one plane to the next.
    /// @param limits the configured maxima to check against.
    /// @return `GeometryError::None` when a layout can be built; otherwise the rule that refused.
    [[nodiscard]] static constexpr GeometryError checkWithStrides(const ImageLayoutRequest& request,
                                                                  std::uint64_t rowStrideBytes,
                                                                  std::uint64_t planeStrideBytes,
                                                                  const EngineLimits& limits) noexcept
    {
        const GeometryError arrangement = checkArrangement(request);
        if (arrangement != GeometryError::None)
        {
            return arrangement;
        }

        const CheckedSize minimumRow = minimumRowBytesOf(request);
        if (!minimumRow.isValid())
        {
            return GeometryError::ArithmeticOverflow;
        }
        if (rowStrideBytes < minimumRow.valueOr(0u))
        {
            return GeometryError::StrideTooSmall;
        }
        if (!isStrideUsable(rowStrideBytes, request))
        {
            return GeometryError::StrideMisaligned;
        }

        const CheckedSize minimumPlane =
            CheckedSize{rowStrideBytes} * CheckedSize{request.extent.height() - 1u} + minimumRow;
        if (!minimumPlane.isValid())
        {
            return GeometryError::ArithmeticOverflow;
        }
        if (planeStrideBytes < minimumPlane.valueOr(0u))
        {
            return GeometryError::StrideTooSmall;
        }
        if (!isStrideUsable(planeStrideBytes, request))
        {
            return GeometryError::StrideMisaligned;
        }

        const CheckedSize total = totalBytesOf(request, rowStrideBytes, planeStrideBytes);
        if (!total.isValid())
        {
            return GeometryError::ArithmeticOverflow;
        }
        if (!total.toAllocationSize().has_value() || !total.toOffset().has_value())
        {
            return GeometryError::SizeNotRepresentable;
        }
        const GeometryError extentError =
            ImageExtent::check(request.extent.width(), request.extent.height(), request.extent.channelCount(), limits);
        if (extentError != GeometryError::None)
        {
            return extentError;
        }
        if (total.valueOr(0u) > limits.maxImageBytes)
        {
            return GeometryError::ImageTooLarge;
        }
        return GeometryError::None;
    }

    /// A layout with these stated strides, where they describe one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param request the extent, sample size, arrangement and alignment the strides must satisfy.
    /// @param rowStrideBytes distance in bytes from one row to the next within a plane.
    /// @param planeStrideBytes distance in bytes from one plane to the next.
    /// @param limits the configured maxima to check against.
    /// @return the layout, keeping the strides exactly as given, or an empty optional exactly when
    ///         `checkWithStrides` would name a reason.
    [[nodiscard]] static constexpr std::optional<ImageLayout> createWithStrides(const ImageLayoutRequest& request,
                                                                                std::uint64_t rowStrideBytes,
                                                                                std::uint64_t planeStrideBytes,
                                                                                const EngineLimits& limits) noexcept
    {
        if (checkWithStrides(request, rowStrideBytes, planeStrideBytes, limits) != GeometryError::None)
        {
            return std::nullopt;
        }
        return ImageLayout{request, rowStrideBytes, planeStrideBytes};
    }

    /// The image's pixel dimensions.
    ///
    /// @return the validated extent this layout describes.
    [[nodiscard]] constexpr const ImageExtent& extent() const noexcept
    {
        return m_extent;
    }

    /// Bytes in one sample, never zero.
    ///
    /// @return the validated element size.
    [[nodiscard]] constexpr std::uint32_t elementSizeBytes() const noexcept
    {
        return m_elementSizeBytes;
    }

    /// Samples of one pixel held in the same plane, never zero.
    ///
    /// @return the validated samples per pixel.
    [[nodiscard]] constexpr std::uint32_t samplesPerPixel() const noexcept
    {
        return m_samplesPerPixel;
    }

    /// Planes the image is stored in, never zero.
    ///
    /// @return the validated plane count.
    [[nodiscard]] constexpr std::uint32_t planeCount() const noexcept
    {
        return m_planeCount;
    }

    /// Distance in bytes from one row to the next within a plane.
    ///
    /// @return the stride as stated or derived, never normalised, never below `minimumRowBytes()`.
    [[nodiscard]] constexpr std::uint64_t rowStrideBytes() const noexcept
    {
        return m_rowStrideBytes;
    }

    /// Distance in bytes from one plane to the next.
    ///
    /// @return the stride as stated or derived, never normalised.
    [[nodiscard]] constexpr std::uint64_t planeStrideBytes() const noexcept
    {
        return m_planeStrideBytes;
    }

    /// Bytes of samples in one row, without whatever padding follows them.
    ///
    /// @return width times samples per pixel times element size, proved representable at
    ///         construction.
    [[nodiscard]] constexpr std::uint64_t minimumRowBytes() const noexcept
    {
        return static_cast<std::uint64_t>(m_extent.width()) * static_cast<std::uint64_t>(m_samplesPerPixel) *
               static_cast<std::uint64_t>(m_elementSizeBytes);
    }

    /// Bytes a buffer must have for this layout.
    ///
    /// It counts the padding between rows and planes, and stops at the last sample: the padding a
    /// stride would put after the final row is not memory anything reads or writes, so requiring it
    /// would refuse buffers that are exactly large enough.
    ///
    /// @return the byte count, proved at construction to be representable, addressable on this
    ///         platform, and within the configured maximum image size.
    [[nodiscard]] constexpr std::uint64_t totalBytes() const noexcept
    {
        return (static_cast<std::uint64_t>(m_planeCount) - 1u) * m_planeStrideBytes +
               (static_cast<std::uint64_t>(m_extent.height()) - 1u) * m_rowStrideBytes + minimumRowBytes();
    }

    /// Whether the samples occupy the buffer with no gaps at all.
    ///
    /// The question a kernel asks before treating the whole image as one span instead of walking it
    /// row by row.
    ///
    /// @return true when no padding exists between rows or between planes.
    [[nodiscard]] constexpr bool isContiguous() const noexcept
    {
        return m_rowStrideBytes == minimumRowBytes() &&
               m_planeStrideBytes == static_cast<std::uint64_t>(m_extent.height()) * m_rowStrideBytes;
    }

    /// Byte offset of the start of a row, from the start of the buffer.
    ///
    /// @param planeIndex plane to address, below `planeCount()`.
    /// @param y row within the plane, below the extent's height.
    /// @return the offset, or an empty optional when either index is out of range.
    [[nodiscard]] constexpr std::optional<std::uint64_t> rowByteOffset(std::uint32_t planeIndex,
                                                                       std::uint32_t y) const noexcept
    {
        if (planeIndex >= m_planeCount || y >= m_extent.height())
        {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(planeIndex) * m_planeStrideBytes +
               static_cast<std::uint64_t>(y) * m_rowStrideBytes;
    }

    /// Byte offset of one sample, from the start of the buffer.
    ///
    /// Checked access, for tests, parsers and cold code. A kernel takes a row offset once and walks
    /// the row; it does not call this per sample.
    ///
    /// @param planeIndex plane to address, below `planeCount()`.
    /// @param y row within the plane, below the extent's height.
    /// @param x column within the row, below the extent's width.
    /// @param sampleInPixel which of the pixel's samples in this plane, below `samplesPerPixel()`.
    /// @return the offset, which is always at least `elementSizeBytes()` below `totalBytes()`, or
    ///         an empty optional when any index is out of range.
    [[nodiscard]] constexpr std::optional<std::uint64_t> sampleByteOffset(std::uint32_t planeIndex, std::uint32_t y,
                                                                          std::uint32_t x,
                                                                          std::uint32_t sampleInPixel) const noexcept
    {
        if (x >= m_extent.width() || sampleInPixel >= m_samplesPerPixel)
        {
            return std::nullopt;
        }
        const std::optional<std::uint64_t> rowOffset = rowByteOffset(planeIndex, y);
        if (!rowOffset.has_value())
        {
            return std::nullopt;
        }
        const std::uint64_t withinRow = (static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(m_samplesPerPixel) +
                                         static_cast<std::uint64_t>(sampleInPixel)) *
                                        static_cast<std::uint64_t>(m_elementSizeBytes);
        return *rowOffset + withinRow;
    }

    /// Two layouts are equal when they describe the same samples in the same places.
    friend constexpr bool operator==(const ImageLayout& left, const ImageLayout& right) noexcept = default;

private:
    /// Builds the value once the request and strides have been checked. Private, so that every
    /// layout in existence has been through one of the check entry points.
    ///
    /// @param request the checked request.
    /// @param rowStrideBytes the checked row stride.
    /// @param planeStrideBytes the checked plane stride.
    constexpr ImageLayout(const ImageLayoutRequest& request, std::uint64_t rowStrideBytes,
                          std::uint64_t planeStrideBytes) noexcept
        : m_extent(request.extent), m_elementSizeBytes(request.elementSizeBytes),
          m_samplesPerPixel(request.samplesPerPixel), m_planeCount(request.planeCount),
          m_rowStrideBytes(rowStrideBytes), m_planeStrideBytes(planeStrideBytes)
    {
    }

    /// The part of a request that is true before any stride is known: nothing is zero, the
    /// alignment is an alignment, and the planes account for exactly the channels of the
    /// already validated extent.
    ///
    /// @param request the request to validate.
    /// @return `GeometryError::None`, or the rule that refused.
    [[nodiscard]] static constexpr GeometryError checkArrangement(const ImageLayoutRequest& request) noexcept
    {
        if (request.elementSizeBytes == 0u || request.samplesPerPixel == 0u || request.planeCount == 0u)
        {
            return GeometryError::ZeroDimension;
        }
        if (!isPowerOfTwo(request.rowAlignmentBytes))
        {
            return GeometryError::AlignmentNotPowerOfTwo;
        }
        if (!isPowerOfTwo(request.elementSizeBytes))
        {
            return GeometryError::ElementSizeNotPowerOfTwo;
        }
        const std::uint64_t storedChannels =
            static_cast<std::uint64_t>(request.samplesPerPixel) * static_cast<std::uint64_t>(request.planeCount);
        if (storedChannels != static_cast<std::uint64_t>(request.extent.channelCount()))
        {
            return GeometryError::PlaneCountMismatch;
        }
        return GeometryError::None;
    }

    /// Bytes of samples in one row of the requested arrangement.
    ///
    /// @param request the request to measure.
    /// @return the product, or an overflowed value when there is no such number.
    [[nodiscard]] static constexpr CheckedSize minimumRowBytesOf(const ImageLayoutRequest& request) noexcept
    {
        return CheckedSize{request.extent.width()} * CheckedSize{request.samplesPerPixel} *
               CheckedSize{request.elementSizeBytes};
    }

    /// The smallest row stride the requested alignment allows.
    ///
    /// @param request the request to measure.
    /// @return the stride, or an overflowed value when there is no such number.
    [[nodiscard]] static constexpr CheckedSize derivedRowStride(const ImageLayoutRequest& request) noexcept
    {
        return alignUp(minimumRowBytesOf(request), request.rowAlignmentBytes);
    }

    /// A whole image of rows at the given stride, which is the plane stride `create` derives.
    ///
    /// @param request the request to measure.
    /// @param rowStride the row stride to multiply out.
    /// @return the stride, or an overflowed value when there is no such number.
    [[nodiscard]] static constexpr CheckedSize derivedPlaneStride(const ImageLayoutRequest& request,
                                                                  CheckedSize rowStride) noexcept
    {
        return rowStride * CheckedSize{request.extent.height()};
    }

    /// Whether a stride can be used at all: a whole number of elements, and on the requested
    /// alignment boundary. A stride that is not a multiple of the element size puts every row after
    /// the first at an address no typed pointer can reach.
    ///
    /// @param strideBytes the stride to test.
    /// @param request the request whose element size and alignment it must satisfy.
    /// @return true when the stride satisfies both.
    [[nodiscard]] static constexpr bool isStrideUsable(std::uint64_t strideBytes,
                                                       const ImageLayoutRequest& request) noexcept
    {
        return strideBytes % static_cast<std::uint64_t>(request.elementSizeBytes) == 0u &&
               isAligned(strideBytes, request.rowAlignmentBytes);
    }

    /// Bytes a buffer must have for a request with these strides.
    ///
    /// @param request the request to measure.
    /// @param rowStrideBytes the row stride.
    /// @param planeStrideBytes the plane stride.
    /// @return the byte count, or an overflowed value when there is no such number.
    [[nodiscard]] static constexpr CheckedSize totalBytesOf(const ImageLayoutRequest& request,
                                                            std::uint64_t rowStrideBytes,
                                                            std::uint64_t planeStrideBytes) noexcept
    {
        return CheckedSize{planeStrideBytes} * CheckedSize{request.planeCount - 1u} +
               CheckedSize{rowStrideBytes} * CheckedSize{request.extent.height() - 1u} + minimumRowBytesOf(request);
    }

    /// The image's validated pixel dimensions.
    ImageExtent m_extent;

    /// Validated bytes per sample.
    std::uint32_t m_elementSizeBytes;

    /// Validated samples of one pixel per plane.
    std::uint32_t m_samplesPerPixel;

    /// Validated number of planes.
    std::uint32_t m_planeCount;

    /// Validated distance in bytes between rows.
    std::uint64_t m_rowStrideBytes;

    /// Validated distance in bytes between planes.
    std::uint64_t m_planeStrideBytes;
};

} // namespace an

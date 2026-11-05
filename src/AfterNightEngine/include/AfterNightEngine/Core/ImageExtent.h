// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How many pixels an image has, checked before anything believes it.
///
/// Three numbers, and a type rather than a struct of three integers for one reason: the numbers are
/// only meaningful together. Each of them can be perfectly ordinary while the count they multiply
/// out to is larger than can be counted, and every later computation - a byte size, a stride, an
/// offset - starts from that product. Validating once, here, is what lets the layers above compute
/// with these values instead of re-checking them.
///
/// An extent carries no meaning beyond counting. Which channel is red, what the samples encode and
/// how they are arranged in memory are questions for the descriptor and the layout; this is the part
/// that has to be true before any of those can be asked.

#pragma once

#include <AfterNightEngine/Core/CheckedSize.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/GeometryError.h>

#include <cstdint>
#include <optional>

namespace an
{

/// The pixel dimensions of an image, validated against the configured limits.
///
/// There is no way to build one that has not been checked: the constructor is private and the two
/// static entry points below are the only way in. So a function taking an `ImageExtent` knows its
/// width, height and channel count are nonzero, within the limits it was checked against, and that
/// their product is a number this arithmetic can hold.
///
/// Trivially copyable and usable in a constant expression.
class ImageExtent
{
public:
    /// Whether an extent can be built from these dimensions, and if not, why not.
    ///
    /// This is the entry point for a caller that has to report the reason - to a user, or as the
    /// context of a failure it returns. A caller that only needs the value uses `create`.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param width image width in pixels.
    /// @param height image height in pixels.
    /// @param channelCount number of channels per pixel.
    /// @param limits the configured maxima to check against.
    /// @return `GeometryError::None` when an extent can be built; otherwise the first rule that
    ///         refused, facts before policy as `GeometryError` describes.
    [[nodiscard]] static constexpr GeometryError check(std::uint32_t width, std::uint32_t height,
                                                       std::uint32_t channelCount, const EngineLimits& limits) noexcept
    {
        if (width == 0u || height == 0u || channelCount == 0u)
        {
            return GeometryError::ZeroDimension;
        }
        if (!sampleCountOf(width, height, channelCount).isValid())
        {
            return GeometryError::ArithmeticOverflow;
        }
        if (width > limits.maxImageWidth || height > limits.maxImageHeight)
        {
            return GeometryError::DimensionTooLarge;
        }
        if (channelCount > limits.maxChannelCount)
        {
            return GeometryError::ChannelCountTooLarge;
        }
        return GeometryError::None;
    }

    /// An extent for these dimensions, where they describe one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param width image width in pixels.
    /// @param height image height in pixels.
    /// @param channelCount number of channels per pixel.
    /// @param limits the configured maxima to check against.
    /// @return the extent, or an empty optional exactly when `check` would name a reason. Nothing
    ///         is allocated and nothing is mutated on refusal.
    [[nodiscard]] static constexpr std::optional<ImageExtent>
    create(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount, const EngineLimits& limits) noexcept
    {
        if (check(width, height, channelCount, limits) != GeometryError::None)
        {
            return std::nullopt;
        }
        return ImageExtent{width, height, channelCount};
    }

    /// Image width in pixels, never zero.
    ///
    /// @return the validated width.
    [[nodiscard]] constexpr std::uint32_t width() const noexcept
    {
        return m_width;
    }

    /// Image height in pixels, never zero.
    ///
    /// @return the validated height.
    [[nodiscard]] constexpr std::uint32_t height() const noexcept
    {
        return m_height;
    }

    /// Channels per pixel, never zero.
    ///
    /// @return the validated channel count.
    [[nodiscard]] constexpr std::uint32_t channelCount() const noexcept
    {
        return m_channelCount;
    }

    /// Pixels in one channel of the image.
    ///
    /// Returned as a plain number rather than a checked one because construction already proved the
    /// larger product it is part of is representable.
    ///
    /// @return width times height.
    [[nodiscard]] constexpr std::uint64_t pixelCount() const noexcept
    {
        return static_cast<std::uint64_t>(m_width) * static_cast<std::uint64_t>(m_height);
    }

    /// Samples in the whole image, counting every channel.
    ///
    /// This is a count of samples and not of bytes: what a sample costs is a property of the
    /// layout, which is where sample size lives.
    ///
    /// @return width times height times channel count, proved representable at construction.
    [[nodiscard]] constexpr std::uint64_t sampleCount() const noexcept
    {
        return pixelCount() * static_cast<std::uint64_t>(m_channelCount);
    }

    /// Two extents are equal when all three dimensions are equal.
    friend constexpr bool operator==(const ImageExtent& left, const ImageExtent& right) noexcept = default;

private:
    /// Builds the value once its dimensions have been checked. Private, so that every extent in
    /// existence has been through `check`.
    ///
    /// @param width validated width.
    /// @param height validated height.
    /// @param channelCount validated channel count.
    constexpr ImageExtent(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount) noexcept
        : m_width(width), m_height(height), m_channelCount(channelCount)
    {
    }

    /// The sample count as checked arithmetic, for the construction-time test that it exists.
    ///
    /// @param width candidate width.
    /// @param height candidate height.
    /// @param channelCount candidate channel count.
    /// @return the product, or an overflowed value when there is no such number.
    [[nodiscard]] static constexpr CheckedSize sampleCountOf(std::uint32_t width, std::uint32_t height,
                                                             std::uint32_t channelCount) noexcept
    {
        return CheckedSize{width} * CheckedSize{height} * CheckedSize{channelCount};
    }

    /// Validated width in pixels.
    std::uint32_t m_width;

    /// Validated height in pixels.
    std::uint32_t m_height;

    /// Validated channels per pixel.
    std::uint32_t m_channelCount;
};

} // namespace an

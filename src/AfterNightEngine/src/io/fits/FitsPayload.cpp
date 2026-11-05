// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - which FITS images decode straight from their payload, and the
// conversion that finishes them.
//
// Where an image's decoded samples are its stored samples with their bytes swapped and, for the
// unsigned zero-point convention, the sign bit flipped, the library's conversion adds nothing but its
// own buffering: it reads a few tens of kilobytes at a time and converts each piece through a generic
// scaling loop. Those images are read from a second handle instead, one strip per call, and
// converted in place here. The bits are the library's own: at unit scale with the signed or unsigned zero
// point it copies or flips the sign bit and nothing else, and no null value is substituted because
// the adapter never asks for one.

#include "io/fits/FitsFile.h"

#include <bit>
#include <cstdint>

namespace an
{
namespace
{

/// One stored sample with its bytes reversed, spelled in shifts and masks a compiler can apply to a
/// vector of samples at once.
///
/// @param word the sample as read.
/// @return the sample in the host's byte order on a little-endian host.
[[nodiscard]] constexpr std::uint16_t reversed(std::uint16_t word) noexcept
{
    return static_cast<std::uint16_t>((word << 8u) | (word >> 8u));
}

/// @copydoc reversed(std::uint16_t)
[[nodiscard]] constexpr std::uint32_t reversed(std::uint32_t word) noexcept
{
    word = (word << 16u) | (word >> 16u);
    return ((word & 0x00ff00ffu) << 8u) | ((word >> 8u) & 0x00ff00ffu);
}

/// @copydoc reversed(std::uint16_t)
[[nodiscard]] constexpr std::uint64_t reversed(std::uint64_t word) noexcept
{
    word = (word << 32u) | (word >> 32u);
    word = ((word & 0x0000ffff0000ffffull) << 16u) | ((word >> 16u) & 0x0000ffff0000ffffull);
    return ((word & 0x00ff00ff00ff00ffull) << 8u) | ((word >> 8u) & 0x00ff00ff00ff00ffull);
}

static_assert(reversed(std::uint16_t{0x1234u}) == 0x3412u);
static_assert(reversed(std::uint32_t{0x12345678u}) == 0x78563412u);
static_assert(reversed(std::uint64_t{0x0123456789abcdefull}) == 0xefcdab8967452301ull);

/// Brings every sample of `count` to the host's byte order in place and flips the sign bits `flip`
/// holds.
///
/// @tparam WordT the unsigned integer of the sample's width.
/// @param bytes the samples, aligned to their width.
/// @param count how many.
/// @param flip the sign bit to flip, or zero.
template <typename WordT> void swapInPlace(std::byte* bytes, std::uint64_t count, WordT flip) noexcept
{
    WordT* const words = reinterpret_cast<WordT*>(bytes);
    if constexpr (std::endian::native == std::endian::big)
    {
        for (std::uint64_t index = 0u; flip != 0u && index < count; ++index)
        {
            words[index] = static_cast<WordT>(words[index] ^ flip);
        }
    }
    else
    {
        for (std::uint64_t index = 0u; index < count; ++index)
        {
            words[index] = static_cast<WordT>(reversed(words[index]) ^ flip);
        }
    }
}

} // namespace

std::optional<FitsPayloadTransform> fitsPayloadTransform(const FitsHeader& header) noexcept
{
    if (header.compressed || header.scale != 1.0)
    {
        return std::nullopt;
    }
    switch (header.bitpix)
    {
    case BYTE_IMG:
        if (header.zero == 0.0 || header.zero == -128.0)
        {
            return FitsPayloadTransform{.sampleBytes = 1u, .flipSign = header.zero != 0.0};
        }
        break;
    case SHORT_IMG:
        if (header.zero == 0.0 || header.zero == 32768.0)
        {
            return FitsPayloadTransform{.sampleBytes = 2u, .flipSign = header.zero != 0.0};
        }
        break;
    case LONG_IMG:
        if (header.zero == 0.0 || header.zero == 2147483648.0)
        {
            return FitsPayloadTransform{.sampleBytes = 4u, .flipSign = header.zero != 0.0};
        }
        break;
    case FLOAT_IMG:
        if (header.zero == 0.0)
        {
            return FitsPayloadTransform{.sampleBytes = 4u, .flipSign = false};
        }
        break;
    case DOUBLE_IMG:
        if (header.zero == 0.0)
        {
            return FitsPayloadTransform{.sampleBytes = 8u, .flipSign = false};
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

void convertFitsPayload(std::byte* bytes, std::uint64_t samples, FitsPayloadTransform transform) noexcept
{
    switch (transform.sampleBytes)
    {
    case 1u:
        if (transform.flipSign)
        {
            for (std::uint64_t index = 0u; index < samples; ++index)
            {
                bytes[index] ^= std::byte{0x80};
            }
        }
        break;
    case 2u:
        swapInPlace<std::uint16_t>(bytes, samples, transform.flipSign ? std::uint16_t{0x8000u} : std::uint16_t{0u});
        break;
    case 4u:
        swapInPlace<std::uint32_t>(bytes, samples, transform.flipSign ? std::uint32_t{0x80000000u} : 0u);
        break;
    case 8u:
        swapInPlace<std::uint64_t>(bytes, samples, 0u);
        break;
    default:
        break;
    }
}

} // namespace an

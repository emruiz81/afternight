// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The alignments the engine asks for, and the arithmetic that honours them.
///
/// Two things about alignment go wrong quietly. The first is that the usual mask arithmetic,
/// `(value + alignment - 1) & ~(alignment - 1)`, silently produces nonsense for an alignment that is
/// not a power of two and a smaller number than it was given for a value near the top of the range -
/// so both are refusals here rather than results. The second is that the constants get copied: a 64
/// appears in an allocator, another in a stride computation, and one of them is later changed. There
/// is one of each here, and each says what it is for, because they are equal today for unrelated
/// reasons and a wider vector or a different cache line would separate them.

#pragma once

#include <AfterNightEngine/Core/CheckedSize.h>

#include <cstdint>

namespace an
{

/// Bytes in a cache line on the platforms this engine targets.
///
/// Buffers are aligned to it so that a row does not begin in the middle of a line, which is what
/// makes a sequential pass over an image read each line once rather than twice, and what keeps two
/// threads writing adjacent rows out of the same line.
inline constexpr std::uint64_t kCacheLineBytes = 64u;

/// Bytes in the widest vector register the packaged instruction sets use.
///
/// A load at this alignment is what lets a kernel use aligned vector loads for the body of a row
/// instead of unaligned ones, and it is 64 because the packaged set includes 512-bit vectors.
inline constexpr std::uint64_t kWidestVectorBytes = 64u;

/// The alignment the engine asks its allocator for by default.
///
/// It is the larger of the two constants above rather than either of them, because a buffer has to
/// satisfy both reasons at once and the two are equal today by coincidence rather than by
/// definition.
inline constexpr std::uint64_t kDefaultBufferAlignmentBytes =
    kCacheLineBytes > kWidestVectorBytes ? kCacheLineBytes : kWidestVectorBytes;

/// Whether a number is a power of two, and therefore usable as an alignment.
///
/// Zero is not: it is the value that turns the usual rounding arithmetic into a division by zero or
/// a mask of every bit, depending on how it is written.
///
/// @param value the number to test.
/// @return true when `value` is a power of two; false for zero and for every other number.
[[nodiscard]] constexpr bool isPowerOfTwo(std::uint64_t value) noexcept
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

/// Whether a count or an offset already sits on an alignment boundary.
///
/// @param value the number to test. Zero is aligned to everything.
/// @param alignment the boundary, which must be a power of two.
/// @return true when `value` is a multiple of `alignment`; false when it is not, and false for an
///         `alignment` that is not a power of two, because nothing satisfies an alignment that is
///         not one.
[[nodiscard]] constexpr bool isAligned(std::uint64_t value, std::uint64_t alignment) noexcept
{
    return isPowerOfTwo(alignment) && (value & (alignment - 1u)) == 0u;
}

/// Whether an address sits on an alignment boundary.
///
/// A null pointer is reported as unaligned whatever the alignment. Zero is a multiple of every
/// power of two, but this question is only ever asked about a buffer, and a null pointer is not
/// one - answering true would let an absent buffer pass the check its presence was meant to prove.
///
/// @param pointer the address to test; may be null.
/// @param alignment the boundary, which must be a power of two.
/// @return true when `pointer` is non-null and its address is a multiple of `alignment`.
[[nodiscard]] inline bool isPointerAligned(const void* pointer, std::uint64_t alignment) noexcept
{
    return pointer != nullptr &&
           isAligned(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer)), alignment);
}

/// The smallest multiple of `alignment` that is not smaller than `value`.
///
/// @param value the count to round up.
/// @param alignment the boundary, which must be a power of two.
/// @return the rounded count; an overflowed value when `value` overflowed, when `alignment` is not
///         a power of two, or when no multiple of `alignment` that large is representable.
[[nodiscard]] constexpr CheckedSize alignUp(CheckedSize value, std::uint64_t alignment) noexcept
{
    if (!value.isValid() || !isPowerOfTwo(alignment))
    {
        return CheckedSize::overflowed();
    }
    const std::uint64_t remainder = value.valueOr(0u) & (alignment - 1u);
    return remainder == 0u ? value : value + CheckedSize{alignment - remainder};
}

/// The largest multiple of `alignment` that is not larger than `value`.
///
/// @param value the count to round down.
/// @param alignment the boundary, which must be a power of two.
/// @return the rounded count; an overflowed value when `value` overflowed or `alignment` is not a
///         power of two.
[[nodiscard]] constexpr CheckedSize alignDown(CheckedSize value, std::uint64_t alignment) noexcept
{
    if (!value.isValid() || !isPowerOfTwo(alignment))
    {
        return CheckedSize::overflowed();
    }
    return CheckedSize{value.valueOr(0u) & ~(alignment - 1u)};
}

} // namespace an

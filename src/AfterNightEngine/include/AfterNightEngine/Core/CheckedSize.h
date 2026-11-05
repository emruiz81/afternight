// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Counting bytes, samples and offsets without wrapping.
///
/// Width times height times channels times sample size overflows a 32-bit computation long before it
/// overflows a real machine, and it overflows 64 bits for a geometry a damaged header can still
/// describe. What makes that dangerous is not the wrap itself but what follows it: a wrapped product
/// is a small, entirely plausible byte count, the allocation that uses it succeeds, and the writes
/// that run past it are a heap corruption with no diagnostic anywhere near its cause.
///
/// So every size the engine computes goes through this type. It carries either a number or the fact
/// that there is no number, propagates the second through further arithmetic, and hands back a value
/// only where a caller has to look at the state to get one. The check happens once, at the end of a
/// chain, which is the only version of this discipline that actually gets written.
///
/// This is deliberately below the engine's result and error types: a failure here is a single fact -
/// the value is not representable - and wrapping it in a code, a message and a context would cost
/// more at every arithmetic site than it could tell a caller. The layers above translate.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace an
{

/// A 64-bit count of bytes, samples or elements that refuses to wrap.
///
/// A value is either representable - a number this type carries - or overflowed, which is the state
/// every operation that cannot produce a representable answer returns. Overflowed is contagious:
/// any arithmetic involving one produces another, so a chain of operations needs one check at its
/// end rather than a check between every step.
///
/// Trivially copyable, 16 bytes, and usable in a constant expression, so a buffer whose size is
/// known at compile time is sized by the same code that sizes one known at run time.
class CheckedSize
{
public:
    /// A representable zero.
    constexpr CheckedSize() noexcept = default;

    /// A representable count.
    ///
    /// Deliberately implicit: these values are written beside plain integers throughout the size
    /// arithmetic, and requiring a cast at each one is how the arithmetic stops being written this
    /// way at all. Sizes in this engine are unsigned everywhere, and a braced construction from a
    /// negative constant is a compile error rather than a very large count.
    ///
    /// @param value the count to carry.
    constexpr CheckedSize(std::uint64_t value) noexcept : m_value(value), m_valid(true) {}

    /// The state that carries no number.
    ///
    /// @return a value for which `isValid()` is false and which contaminates any arithmetic it
    ///         takes part in.
    [[nodiscard]] static constexpr CheckedSize overflowed() noexcept
    {
        CheckedSize result;
        result.m_valid = false;
        return result;
    }

    /// Whether this carries a number.
    ///
    /// @return true when the value is representable; false when an operation overflowed, went
    ///         below zero, or was given an already overflowed operand.
    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return m_valid;
    }

    /// The number, where there is one.
    ///
    /// @return the count, or an empty optional when this value overflowed.
    [[nodiscard]] constexpr std::optional<std::uint64_t> value() const noexcept
    {
        return m_valid ? std::optional<std::uint64_t>(m_value) : std::nullopt;
    }

    /// The number, or a stated substitute.
    ///
    /// For the call sites where a failure has already been handled and the alternative to this is
    /// a dereference that reads as if it could not fail.
    ///
    /// @param fallback what to return when this value overflowed.
    /// @return the count, or `fallback`.
    [[nodiscard]] constexpr std::uint64_t valueOr(std::uint64_t fallback) const noexcept
    {
        return m_valid ? m_value : fallback;
    }

    /// The number as a size this platform's allocator can be asked for.
    ///
    /// The conversion is checked rather than a cast because `std::size_t` is not always 64 bits: a
    /// count that fits the arithmetic and not the allocator is exactly the case a cast turns into
    /// an allocation of the wrong size.
    ///
    /// @return the count as a `std::size_t`, or an empty optional when this value overflowed or is
    ///         larger than the largest object this platform can describe.
    [[nodiscard]] constexpr std::optional<std::size_t> toAllocationSize() const noexcept
    {
        if (!m_valid)
        {
            return std::nullopt;
        }
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
        {
            if (m_value > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
            {
                return std::nullopt;
            }
        }
        return static_cast<std::size_t>(m_value);
    }

    /// The number as a distance between two pointers into one object.
    ///
    /// A count larger than this can be allocated on some platforms and can never be used as an
    /// offset: subtracting two pointers that far apart is undefined, and the index arithmetic
    /// inside every kernel is written in terms of that distance.
    ///
    /// @return the count as a `std::ptrdiff_t`, or an empty optional when this value overflowed or
    ///         exceeds the largest representable pointer difference.
    [[nodiscard]] constexpr std::optional<std::ptrdiff_t> toOffset() const noexcept
    {
        constexpr std::uint64_t kLargestOffset =
            static_cast<std::uint64_t>((std::numeric_limits<std::ptrdiff_t>::max)());
        if (!m_valid || m_value > kLargestOffset)
        {
            return std::nullopt;
        }
        return static_cast<std::ptrdiff_t>(m_value);
    }

    /// Two representable values are equal when their counts are equal, and any two overflowed
    /// values are equal to each other, because an overflowed value carries nothing to tell them
    /// apart by.
    friend constexpr bool operator==(const CheckedSize& left, const CheckedSize& right) noexcept = default;

    /// The sum, or overflowed when the sum does not fit.
    ///
    /// @param left the first count.
    /// @param right the second count.
    /// @return the sum, or an overflowed value when either operand overflowed or the sum exceeds
    ///         what 64 bits can hold.
    [[nodiscard]] friend constexpr CheckedSize operator+(CheckedSize left, CheckedSize right) noexcept
    {
        if (!left.m_valid || !right.m_valid)
        {
            return CheckedSize::overflowed();
        }
        if (left.m_value > (std::numeric_limits<std::uint64_t>::max)() - right.m_value)
        {
            return CheckedSize::overflowed();
        }
        return CheckedSize{left.m_value + right.m_value};
    }

    /// The difference, or overflowed when it would be negative.
    ///
    /// These counts are unsigned, so a subtraction that goes below zero is the same defect as one
    /// that goes above the top and is reported the same way.
    ///
    /// @param left the count to subtract from.
    /// @param right the count to subtract.
    /// @return the difference, or an overflowed value when either operand overflowed or `right` is
    ///         larger than `left`.
    [[nodiscard]] friend constexpr CheckedSize operator-(CheckedSize left, CheckedSize right) noexcept
    {
        if (!left.m_valid || !right.m_valid || left.m_value < right.m_value)
        {
            return CheckedSize::overflowed();
        }
        return CheckedSize{left.m_value - right.m_value};
    }

    /// The product, or overflowed when the product does not fit.
    ///
    /// @param left the first count.
    /// @param right the second count.
    /// @return the product, or an overflowed value when either operand overflowed or the product
    ///         exceeds what 64 bits can hold.
    [[nodiscard]] friend constexpr CheckedSize operator*(CheckedSize left, CheckedSize right) noexcept
    {
        if (!left.m_valid || !right.m_valid)
        {
            return CheckedSize::overflowed();
        }
        if (left.m_value != 0u && right.m_value > (std::numeric_limits<std::uint64_t>::max)() / left.m_value)
        {
            return CheckedSize::overflowed();
        }
        return CheckedSize{left.m_value * right.m_value};
    }

private:
    /// The count, held at zero while `m_valid` is false so that any two overflowed values compare
    /// equal rather than by whatever arithmetic produced them.
    std::uint64_t m_value = 0u;

    /// Whether `m_value` is a number this type is carrying rather than the absence of one.
    bool m_valid = true;
};

} // namespace an

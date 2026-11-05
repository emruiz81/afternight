// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one row body of a linear presentation of narrow codes, included where it is compiled.
//
// This file is not a header. It defines the byte an eight- or sixteen-bit code presents linearly as, and the row
// loop that presents every n-th code of a run. The display presentation includes it inside an unnamed namespace as
// the scalar authority, and the presentation CPU unit includes it inside the vector library's per-target namespace,
// where the same loop finishes every compiled row. Writing the rule once is what makes "the compiled row writes the
// scalar authority's bytes" structural: the vector body takes the same three sixteen-bit steps the scalar rule takes,
// and its tail is the scalar rule.
//
// The includer supplies `<cstdint>`.

/// The byte an eight-bit code presents linearly as: the code itself, since full scale is the byte's.
///
/// @param code the code.
/// @return the byte.
[[nodiscard]] constexpr std::uint8_t presentedByteOfCode(std::uint8_t code) noexcept
{
    return code;
}

/// The byte a sixteen-bit code presents linearly as: its fraction of full scale, rounded to the nearest byte.
///
/// 65535 is 255 times 257, so that byte is the code over 257 rounded, and 257 is odd, so no quotient is a tie. It is
/// formed in three steps none of which leaves sixteen bits: the high half of the code times 65281, plus 128, shifted
/// right by eight. That is the rounded quotient for every one of the 65536 codes, and it is the form a vector takes
/// sixteen or thirty-two codes at a time.
///
/// @param code the code.
/// @return the byte.
[[nodiscard]] constexpr std::uint8_t presentedByteOfCode(std::uint16_t code) noexcept
{
    const auto high = static_cast<std::uint16_t>((static_cast<std::uint32_t>(code) * 65281u) >> 16u);
    return static_cast<std::uint8_t>(static_cast<std::uint16_t>(high + 128u) >> 8u);
}

/// Presents every `step`-th code of a run as bytes, from output position `first` to `count`.
///
/// @tparam CodeT `std::uint8_t` or `std::uint16_t`.
/// @param source the run's first code.
/// @param step codes between two presented ones; at least one.
/// @param destination the row's first byte.
/// @param first the first output position written.
/// @param count one past the last output position written.
template <typename CodeT>
inline void presentCodeRow(const CodeT* source, std::uint64_t step, std::uint8_t* destination, std::uint64_t first,
                           std::uint64_t count) noexcept
{
    for (std::uint64_t x = first; x < count; ++x)
    {
        destination[x] = presentedByteOfCode(source[x * step]);
    }
}

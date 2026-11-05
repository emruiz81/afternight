// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Which colour filter covered each pixel of a sensor frame.
///
/// A colour camera records one plane, with a repeating grid of filters over it. The pattern names
/// which filter sits over the pixel at the mosaic origin, and everything downstream - demosaicing,
/// calibration that must not average across filters, drizzle that must place samples per filter -
/// reads it.
///
/// The phase is the part that is easy to leave out and expensive to be wrong about. A pattern alone
/// is only true of the frame as it came off the sensor: crop one column and every pixel is under a
/// different filter, so the pattern that described the frame now describes its neighbour's colours.
/// Carrying the phase beside the pattern is what lets a crop stay describable instead of having to
/// be forbidden or silently mis-demosaiced.
///
/// **Two shapes of mosaic are described, and the second one is a table.** The four two-by-two Bayer
/// arrangements are named, because naming them is enough: the name fixes all four filters. A
/// six-by-six mosaic is not one arrangement but a per-camera table, so the descriptor carries the
/// thirty-six entries themselves and validates them against the rules such a mosaic satisfies.
/// Approximating one by the nearest Bayer pattern stays refused, and so does a table whose entries
/// do not describe a filter array at all - plausible colour with wrong detail is worse than a named
/// refusal, and a table nobody checked is how the wrong detail gets in.
///
/// What the two shapes share is that a pixel's filter is a question with one answer. `colorAt` is
/// that answer for both, phase included, so a kernel that walks a mosaic is written once against a
/// descriptor rather than twice against two spellings of the same fact.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/DescriptorError.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace an
{

/// Which filter sits over one pixel of a mosaic.
///
/// Three colours rather than four: a mosaic with two green filters of different transmission is
/// still a mosaic of red, green and blue, and the difference between the two greens belongs to
/// calibration rather than to the description of the filter array. The numbering matches the order
/// every raw decoder reports, which is what keeps a table read from a file from needing a
/// translation on the way in.
enum class CfaColor : std::uint8_t
{
    /// The red filter.
    Red,

    /// A green filter.
    Green,

    /// The blue filter.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Blue
};

/// Whether a value is one of the filter colours this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param color the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isCfaColor(CfaColor color) noexcept
{
    return static_cast<std::uint8_t>(color) <= static_cast<std::uint8_t>(CfaColor::Blue);
}

/// The filter colour's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// The spelling of the enumerator: `CfaColor::Red` is `"Red"`.
///
/// Thread-safe and allocation-free.
///
/// @param color the colour to name. A value outside the enumeration is named `"UnknownCfaColor"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cfaColorName(CfaColor color) noexcept;

/// How many pixels a tabulated mosaic repeats over, in each direction.
inline constexpr std::uint32_t kCfaTableSide = 6u;

/// How many entries a tabulated mosaic's repeating block holds.
inline constexpr std::size_t kCfaTableEntries =
    static_cast<std::size_t>(kCfaTableSide) * static_cast<std::size_t>(kCfaTableSide);

/// The thirty-six filter colours of one repeating block, in row-major order from the block origin.
///
/// The argument type of the checking entry points below, which is deliberate: a caller assembling a
/// table out of what a decoder reported holds a run of colours and nothing else, and asking it to
/// build a validated type before it can find out whether the values are valid would be the wrong
/// order.
///
/// An aggregate over a fixed array rather than a standard container, for a reason the include budget
/// makes concrete: this type is named by the filter-array descriptor, which every image descriptor
/// carries, so one more standard header here is one more in the closure of the engine's central
/// image header. Thirty-six bytes and two accessors do not need one.
struct CfaColorEntries
{
    /// The entries themselves, row-major from the block origin.
    CfaColor entries[kCfaTableEntries];

    /// One entry.
    ///
    /// @param index the row-major position, below `kCfaTableEntries`.
    /// @return the colour there.
    [[nodiscard]] constexpr CfaColor operator[](std::size_t index) const noexcept
    {
        return entries[index];
    }

    /// One entry, to write.
    ///
    /// @param index the row-major position, below `kCfaTableEntries`.
    /// @return a reference to the colour there.
    [[nodiscard]] constexpr CfaColor& operator[](std::size_t index) noexcept
    {
        return entries[index];
    }

    /// Two runs are equal when all thirty-six entries are equal.
    ///
    /// Written out rather than defaulted, because a defaulted comparison over an array member is
    /// well formed only in the direction nobody needs and the loop is three lines.
    ///
    /// @param left one run.
    /// @param right the other.
    /// @return true when every entry of one equals the entry of the other at the same position.
    [[nodiscard]] friend constexpr bool operator==(const CfaColorEntries& left, const CfaColorEntries& right) noexcept
    {
        for (std::size_t index = 0u; index < kCfaTableEntries; ++index)
        {
            if (left.entries[index] != right.entries[index])
            {
                return false;
            }
        }
        return true;
    }
};

/// A validated six-by-six filter table.
///
/// There is no way to build one whose entries do not describe a mosaic: the constructor that takes
/// entries is private, and `create` is the only way in. The default-constructed value is
/// deliberately *not* a describable table - it is thirty-six red filters, which `check` refuses by
/// name - so a descriptor with no table cannot be mistaken for one carrying a described mosaic.
///
/// Trivially copyable and usable in a constant expression.
class CfaFilterTable
{
public:
    /// Builds the table that describes nothing: thirty-six red entries, which no mosaic has.
    ///
    /// It exists because a descriptor for a mosaic that is not tabulated still has to hold a table
    /// member, and holding one that is unmistakably not a filter array is safer than holding one
    /// that happens to be valid.
    constexpr CfaFilterTable() noexcept = default;

    /// Whether these entries describe a six-by-six filter array, and if not, why not.
    ///
    /// The rules are the ones such a mosaic satisfies by construction, checked in the order that
    /// names the most specific reason: every entry is a colour this build knows, the block holds
    /// eight red, twenty green and eight blue filters, each row and each column holds equally many
    /// red and blue filters and either one or two of each, and every green filter's four-neighbour
    /// cross holds equally many red and blue - with the left and right neighbours the same colour
    /// when it holds two of each.
    ///
    /// The green rule is checked over the whole periodic block. A filter on an edge is adjacent to
    /// the opposite edge of the next repetition, so those wrapped crosses are distinct constraints
    /// and are checked explicitly.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param entries the thirty-six filter colours, row-major from the block origin.
    /// @return `DescriptorError::None` when the entries describe a mosaic; `UnknownCfaColor` for an
    ///         entry outside the enumeration; `CfaTableColorCountsWrong` when the block's totals are
    ///         not eight, twenty and eight; `CfaTableLineCountsWrong` when a row or column breaks
    ///         the per-line rule; `CfaTableGreenNeighbourhoodWrong` when a green filter's cross
    ///         does.
    [[nodiscard]] static constexpr DescriptorError check(const CfaColorEntries& entries) noexcept
    {
        std::uint32_t total[3] = {0u, 0u, 0u};
        for (std::size_t index = 0u; index < kCfaTableEntries; ++index)
        {
            if (!isCfaColor(entries[index]))
            {
                return DescriptorError::UnknownCfaColor;
            }
            ++total[static_cast<std::size_t>(entries[index])];
        }
        if (total[0] != 8u || total[1] != 20u || total[2] != 8u)
        {
            return DescriptorError::CfaTableColorCountsWrong;
        }
        for (std::uint32_t line = 0u; line < kCfaTableSide; ++line)
        {
            std::uint32_t row[3] = {0u, 0u, 0u};
            std::uint32_t column[3] = {0u, 0u, 0u};
            for (std::uint32_t other = 0u; other < kCfaTableSide; ++other)
            {
                ++row[static_cast<std::size_t>(entryAt(entries, line, other))];
                ++column[static_cast<std::size_t>(entryAt(entries, other, line))];
            }
            if (!lineCountsDescribeMosaic(row) || !lineCountsDescribeMosaic(column))
            {
                return DescriptorError::CfaTableLineCountsWrong;
            }
        }
        for (std::uint32_t row = 0u; row < kCfaTableSide; ++row)
        {
            for (std::uint32_t column = 0u; column < kCfaTableSide; ++column)
            {
                if (entryAt(entries, row, column) != CfaColor::Green)
                {
                    continue;
                }
                const std::uint32_t above = (row + kCfaTableSide - 1u) % kCfaTableSide;
                const std::uint32_t below = (row + 1u) % kCfaTableSide;
                const std::uint32_t left = (column + kCfaTableSide - 1u) % kCfaTableSide;
                const std::uint32_t right = (column + 1u) % kCfaTableSide;
                std::uint32_t cross[3] = {0u, 0u, 0u};
                ++cross[static_cast<std::size_t>(entryAt(entries, above, column))];
                ++cross[static_cast<std::size_t>(entryAt(entries, below, column))];
                ++cross[static_cast<std::size_t>(entryAt(entries, row, left))];
                ++cross[static_cast<std::size_t>(entryAt(entries, row, right))];
                if (cross[0] != cross[2] || cross[0] < 1u || cross[0] > 2u ||
                    (cross[0] == 2u && entryAt(entries, row, left) != entryAt(entries, row, right)))
                {
                    return DescriptorError::CfaTableGreenNeighbourhoodWrong;
                }
            }
        }
        return DescriptorError::None;
    }

    /// The table these entries describe, where they describe one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param entries the thirty-six filter colours, row-major from the block origin.
    /// @return the table, or an empty optional exactly when `check` would name a reason. Nothing is
    ///         allocated and nothing is mutated on refusal.
    [[nodiscard]] static constexpr std::optional<CfaFilterTable> create(const CfaColorEntries& entries) noexcept
    {
        if (check(entries) != DescriptorError::None)
        {
            return std::nullopt;
        }
        return CfaFilterTable{entries};
    }

    /// The filter over one entry of the repeating block.
    ///
    /// Both coordinates are reduced into the block, so a caller may pass an image coordinate
    /// directly and does not have to remember which of the two is reduced where.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param row the row, reduced into the block.
    /// @param column the column, reduced into the block.
    /// @return the validated filter colour at that position.
    [[nodiscard]] constexpr CfaColor at(std::uint32_t row, std::uint32_t column) const noexcept
    {
        return entryAt(m_entries, row % kCfaTableSide, column % kCfaTableSide);
    }

    /// The whole block, for a caller writing it out or handing it to a kernel.
    ///
    /// @return the validated entries, row-major from the block origin.
    [[nodiscard]] constexpr const CfaColorEntries& entries() const noexcept
    {
        return m_entries;
    }

    /// Whether this table describes a mosaic rather than being the default that describes none.
    ///
    /// The question a descriptor answers before it reads the table, and the reason the default is
    /// not a valid table: this is a fact about the value rather than a flag beside it.
    ///
    /// @return true when the entries pass `check`.
    [[nodiscard]] constexpr bool describesMosaic() const noexcept
    {
        return check(m_entries) == DescriptorError::None;
    }

    /// Two tables are equal when all thirty-six entries are equal.
    ///
    /// @param left one table.
    /// @param right the other.
    /// @return true when every entry of one equals the entry of the other at the same position.
    friend constexpr bool operator==(const CfaFilterTable& left, const CfaFilterTable& right) noexcept
    {
        return left.m_entries == right.m_entries;
    }

private:
    /// Builds the value once the entries have been checked. Private, so that every table in
    /// existence has been through `check` or is the default that describes nothing.
    ///
    /// @param entries the checked entries.
    constexpr explicit CfaFilterTable(const CfaColorEntries& entries) noexcept : m_entries(entries) {}

    /// One entry of a raw block, without the reduction `at` applies.
    ///
    /// @param entries the block to read.
    /// @param row the row, already inside the block.
    /// @param column the column, already inside the block.
    /// @return the entry at that position.
    [[nodiscard]] static constexpr CfaColor entryAt(const CfaColorEntries& entries, std::uint32_t row,
                                                    std::uint32_t column) noexcept
    {
        return entries[static_cast<std::size_t>(row) * static_cast<std::size_t>(kCfaTableSide) +
                       static_cast<std::size_t>(column)];
    }

    /// Whether one row's or one column's colour counts are the ones a mosaic has.
    ///
    /// @param counts how many of each colour the line holds, indexed by `CfaColor`.
    /// @return true when the red and blue counts are equal, are one or two, and leave the rest of
    ///         the line green.
    [[nodiscard]] static constexpr bool lineCountsDescribeMosaic(const std::uint32_t (&counts)[3]) noexcept
    {
        return counts[0] == counts[2] && counts[0] >= 1u && counts[0] <= 2u &&
               counts[1] + 2u * counts[0] == kCfaTableSide;
    }

    /// The validated entries, or thirty-six red filters for the value that describes nothing.
    CfaColorEntries m_entries{};
};

/// The repeating filter arrangement over a sensor plane.
///
/// Each two-by-two name reads the block in row-major order from the mosaic origin, which is the
/// spelling every camera and file format uses for it. The tabulated arrangement has no such name,
/// because its filters are a per-camera table rather than a permutation of one.
enum class CfaPattern : std::uint8_t
{
    /// No filter array. A monochrome sensor, or an image whose mosaic has already been resolved.
    None,

    /// Red, green over green, blue.
    Rggb,

    /// Blue, green over green, red.
    Bggr,

    /// Green, red over blue, green.
    Grbg,

    /// Green, blue over red, green.
    Gbrg,

    /// A six-by-six mosaic whose filters are carried as a validated table.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    XTrans
};

/// Whether a value is one of the patterns this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param pattern the value to test.
/// @return true when the value is one of the enumerators, `None` included.
[[nodiscard]] constexpr bool isCfaPattern(CfaPattern pattern) noexcept
{
    return static_cast<std::uint8_t>(pattern) <= static_cast<std::uint8_t>(CfaPattern::XTrans);
}

/// Whether the pattern's filters are carried as a table rather than fixed by its name.
///
/// The question that decides which entry point builds a descriptor, so it is asked of the pattern
/// rather than derived from the block size at each call site.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param pattern the pattern to test.
/// @return true for `CfaPattern::XTrans` and false for every other value, an unknown one included.
[[nodiscard]] constexpr bool cfaPatternIsTabulated(CfaPattern pattern) noexcept
{
    return pattern == CfaPattern::XTrans;
}

/// How many pixels the pattern repeats over, in each direction.
///
/// This is the range a phase indexes, so it is the number that decides whether a phase is inside
/// the pattern or names no pixel of it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param pattern the pattern to measure.
/// @return 2 for each named mosaic, `kCfaTableSide` for the tabulated one, and 0 for `None` and for
///         a value outside the enumeration, neither of which has a block for a phase to be inside.
[[nodiscard]] constexpr std::uint32_t cfaPatternBlockSize(CfaPattern pattern) noexcept
{
    switch (pattern)
    {
    case CfaPattern::Rggb:
    case CfaPattern::Bggr:
    case CfaPattern::Grbg:
    case CfaPattern::Gbrg:
        return 2u;
    case CfaPattern::XTrans:
        return kCfaTableSide;
    case CfaPattern::None:
        return 0u;
    }
    return 0u;
}

/// A filter array, where its repeating block starts, and its table when it has one, validated
/// together.
///
/// There is no way to build one whose phase is outside the pattern it indexes, whose tabulated
/// pattern has no table, or whose named pattern carries one: the constructor is private, and the
/// static entry points below are the only way in. The default-constructed value is the image with
/// no mosaic at all, which is a legitimate state rather than an unset one - most images this engine
/// handles have no filter array.
///
/// Trivially copyable and usable in a constant expression.
class CfaDescriptor
{
public:
    /// Builds the filter array of an image that has none: pattern `CfaPattern::None`, a zero phase
    /// and the table that describes nothing.
    ///
    /// This is a legitimate state rather than an unset one, which is why it is the default: most
    /// images this engine handles have no filter array, and every rule that mentions one is
    /// written so that this value satisfies it.
    constexpr CfaDescriptor() noexcept = default;

    /// Whether a descriptor can be built from this named pattern and phase, and if not, why not.
    ///
    /// Facts before combinations: an unknown pattern is refused before its phase is judged, because
    /// an unknown pattern has no block size and testing a phase against zero would report the wrong
    /// reason for the right refusal.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param pattern the filter arrangement. A tabulated one is refused here, because this entry
    ///        point has no table to carry.
    /// @param originX column, within the repeating block, that the image's first column sits at.
    /// @param originY row, within the repeating block, that the image's first row sits at.
    /// @return `DescriptorError::None` when a descriptor can be built; `UnknownCfaPattern` for a
    ///         pattern outside the enumeration; `CfaTableRequired` for a tabulated pattern;
    ///         `CfaOriginNotZero` when a phase is given for an image with no mosaic; and
    ///         `CfaOriginOutOfPattern` when a phase is at or beyond the block size.
    [[nodiscard]] static constexpr DescriptorError check(CfaPattern pattern, std::uint32_t originX,
                                                         std::uint32_t originY) noexcept
    {
        if (!isCfaPattern(pattern))
        {
            return DescriptorError::UnknownCfaPattern;
        }
        if (cfaPatternIsTabulated(pattern))
        {
            return DescriptorError::CfaTableRequired;
        }
        const std::uint32_t block = cfaPatternBlockSize(pattern);
        if (block == 0u)
        {
            return (originX == 0u && originY == 0u) ? DescriptorError::None : DescriptorError::CfaOriginNotZero;
        }
        if (originX >= block || originY >= block)
        {
            return DescriptorError::CfaOriginOutOfPattern;
        }
        return DescriptorError::None;
    }

    /// Whether a descriptor can be built from this filter table and phase, and if not, why not.
    ///
    /// A table is what makes a mosaic tabulated, so this entry point names no pattern: the
    /// descriptor it checks is always `CfaPattern::XTrans`.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param entries the thirty-six filter colours, row-major from the block origin.
    /// @param originX column, within the repeating block, that the image's first column sits at.
    /// @param originY row, within the repeating block, that the image's first row sits at.
    /// @return `DescriptorError::None` when a descriptor can be built; whichever table rule
    ///         `CfaFilterTable::check` refused; or `CfaOriginOutOfPattern` when a phase is at or
    ///         beyond `kCfaTableSide`.
    [[nodiscard]] static constexpr DescriptorError check(const CfaColorEntries& entries, std::uint32_t originX,
                                                         std::uint32_t originY) noexcept
    {
        const DescriptorError tableError = CfaFilterTable::check(entries);
        if (tableError != DescriptorError::None)
        {
            return tableError;
        }
        if (originX >= kCfaTableSide || originY >= kCfaTableSide)
        {
            return DescriptorError::CfaOriginOutOfPattern;
        }
        return DescriptorError::None;
    }

    /// Whether an already-built descriptor still describes a mosaic, and if not, why not.
    ///
    /// Every descriptor in existence has been through one of the two entry points above, so this
    /// can only fail for a value that reached a caller through something other than the type
    /// system - a struct read from a file, a member left behind by a `memcpy`. A composite
    /// descriptor that embeds one re-asks this rather than re-deriving the rules, which is what
    /// keeps the tabulated and the named shapes from being validated two different ways.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param descriptor the value to re-examine.
    /// @return `DescriptorError::None` when the value is one this type could have built, and
    ///         otherwise whichever rule it breaks.
    [[nodiscard]] static constexpr DescriptorError check(const CfaDescriptor& descriptor) noexcept
    {
        if (descriptor.hasFilterTable())
        {
            return check(descriptor.m_table.entries(), descriptor.m_originX, descriptor.m_originY);
        }
        if (descriptor.m_table != CfaFilterTable{})
        {
            return DescriptorError::CfaTableNotAllowed;
        }
        return check(descriptor.m_pattern, descriptor.m_originX, descriptor.m_originY);
    }

    /// A descriptor for this named pattern and phase, where they describe one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param pattern the filter arrangement, which must not be a tabulated one.
    /// @param originX column, within the repeating block, that the image's first column sits at.
    /// @param originY row, within the repeating block, that the image's first row sits at.
    /// @return the descriptor, or an empty optional exactly when `check` would name a reason.
    ///         Nothing is allocated and nothing is mutated on refusal.
    [[nodiscard]] static constexpr std::optional<CfaDescriptor> create(CfaPattern pattern, std::uint32_t originX,
                                                                       std::uint32_t originY) noexcept
    {
        if (check(pattern, originX, originY) != DescriptorError::None)
        {
            return std::nullopt;
        }
        return CfaDescriptor{pattern, CfaFilterTable{}, originX, originY};
    }

    /// A tabulated descriptor for these entries and phase, where they describe one.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param entries the thirty-six filter colours, row-major from the block origin.
    /// @param originX column, within the repeating block, that the image's first column sits at.
    /// @param originY row, within the repeating block, that the image's first row sits at.
    /// @return the descriptor, or an empty optional exactly when `check` would name a reason.
    ///         Nothing is allocated and nothing is mutated on refusal.
    [[nodiscard]] static constexpr std::optional<CfaDescriptor>
    create(const CfaColorEntries& entries, std::uint32_t originX, std::uint32_t originY) noexcept
    {
        const std::optional<CfaFilterTable> table = CfaFilterTable::create(entries);
        if (!table.has_value() || originX >= kCfaTableSide || originY >= kCfaTableSide)
        {
            return std::nullopt;
        }
        return CfaDescriptor{CfaPattern::XTrans, *table, originX, originY};
    }

    /// The filter arrangement.
    ///
    /// @return the validated pattern, which is `CfaPattern::None` for an image with no mosaic.
    [[nodiscard]] constexpr CfaPattern pattern() const noexcept
    {
        return m_pattern;
    }

    /// Where the repeating block starts horizontally.
    ///
    /// @return the validated phase, always below `cfaPatternBlockSize(pattern())` and zero when
    ///         there is no mosaic.
    [[nodiscard]] constexpr std::uint32_t originX() const noexcept
    {
        return m_originX;
    }

    /// Where the repeating block starts vertically.
    ///
    /// @return the validated phase, always below `cfaPatternBlockSize(pattern())` and zero when
    ///         there is no mosaic.
    [[nodiscard]] constexpr std::uint32_t originY() const noexcept
    {
        return m_originY;
    }

    /// Whether this image still has its filter array unresolved.
    ///
    /// The question calibration asks before it decides whether neighbouring pixels are comparable.
    ///
    /// @return true when the pattern is not `CfaPattern::None`.
    [[nodiscard]] constexpr bool isMosaiced() const noexcept
    {
        return m_pattern != CfaPattern::None;
    }

    /// Whether this descriptor's filters are carried as a table.
    ///
    /// @return true exactly when the pattern is `CfaPattern::XTrans`, in which case
    ///         `filterTable()` describes a mosaic.
    [[nodiscard]] constexpr bool hasFilterTable() const noexcept
    {
        return cfaPatternIsTabulated(m_pattern);
    }

    /// The validated six-by-six table.
    ///
    /// @return the table, which describes a mosaic exactly when `hasFilterTable()` is true and is
    ///         otherwise the value that describes none.
    [[nodiscard]] constexpr const CfaFilterTable& filterTable() const noexcept
    {
        return m_table;
    }

    /// Which filter covered the image pixel at these coordinates.
    ///
    /// The one question every mosaic kernel asks, answered for both shapes of mosaic with the phase
    /// already applied - so a kernel walks a descriptor rather than branching on which shape it is.
    /// The coordinates are the image's own, and the reduction into the repeating block happens here.
    ///
    /// Thread-safe, allocation-free, and usable in a constant expression.
    ///
    /// @param x the image column.
    /// @param y the image row.
    /// @return the filter colour over that pixel. An image with no mosaic has no filter over any
    ///         pixel, and reports `CfaColor::Green` - the colour a monochrome plane is treated as
    ///         everywhere it meets this question - so a caller that failed to check `isMosaiced()`
    ///         reads one consistent answer rather than a pattern that is not there.
    [[nodiscard]] constexpr CfaColor colorAt(std::uint32_t x, std::uint32_t y) const noexcept
    {
        if (m_pattern == CfaPattern::XTrans)
        {
            return m_table.at(y + m_originY, x + m_originX);
        }
        if (m_pattern == CfaPattern::None)
        {
            return CfaColor::Green;
        }
        const std::uint32_t row = (y + m_originY) & 1u;
        const std::uint32_t column = (x + m_originX) & 1u;
        return bayerColorAt(m_pattern, row, column);
    }

    /// Two descriptors are equal when the pattern, both phases and the table are equal.
    ///
    /// Written out for the same reason `CfaFilterTable`'s is: a defaulted `noexcept` comparison
    /// over a member holding an array would be deleted.
    ///
    /// @param left one descriptor.
    /// @param right the other.
    /// @return true when the pattern, both phases and the table all agree. A descriptor whose
    ///         pattern is not tabulated holds the table that describes nothing, so comparing the
    ///         table costs nothing and cannot make two otherwise equal descriptors differ.
    friend constexpr bool operator==(const CfaDescriptor& left, const CfaDescriptor& right) noexcept
    {
        return left.m_pattern == right.m_pattern && left.m_originX == right.m_originX &&
               left.m_originY == right.m_originY && left.m_table == right.m_table;
    }

private:
    /// Builds the value once the pattern, table and phase have been checked. Private, so that every
    /// descriptor in existence has been through a `check` or is the no-mosaic default.
    ///
    /// @param pattern the checked pattern.
    /// @param table the checked table, or the value that describes nothing.
    /// @param originX the checked horizontal phase.
    /// @param originY the checked vertical phase.
    constexpr CfaDescriptor(CfaPattern pattern, const CfaFilterTable& table, std::uint32_t originX,
                            std::uint32_t originY) noexcept
        : m_table(table), m_pattern(pattern), m_originX(originX), m_originY(originY)
    {
    }

    /// The filter one of the four named patterns puts at one position of its two-by-two block.
    ///
    /// Two of the arrangements put their green filters on the block's diagonal and two put them
    /// off it, which is the whole of the difference between the two pairs.
    ///
    /// @param pattern one of the four named arrangements.
    /// @param row the row inside the block, zero or one.
    /// @param column the column inside the block, zero or one.
    /// @return the filter colour there; `CfaColor::Green` for anything that is not one of the four,
    ///         which `colorAt` has already excluded.
    [[nodiscard]] static constexpr CfaColor bayerColorAt(CfaPattern pattern, std::uint32_t row,
                                                         std::uint32_t column) noexcept
    {
        const bool onDiagonal = row == column;
        const bool firstRow = row == 0u;
        switch (pattern)
        {
        case CfaPattern::Rggb:
            return onDiagonal ? (firstRow ? CfaColor::Red : CfaColor::Blue) : CfaColor::Green;
        case CfaPattern::Bggr:
            return onDiagonal ? (firstRow ? CfaColor::Blue : CfaColor::Red) : CfaColor::Green;
        case CfaPattern::Grbg:
            return onDiagonal ? CfaColor::Green : (firstRow ? CfaColor::Red : CfaColor::Blue);
        case CfaPattern::Gbrg:
            return onDiagonal ? CfaColor::Green : (firstRow ? CfaColor::Blue : CfaColor::Red);
        case CfaPattern::None:
        case CfaPattern::XTrans:
            break;
        }
        return CfaColor::Green;
    }

    /// The validated table, or the value that describes nothing. First, so that the widest member
    /// starts the object and the two phases pack behind the pattern.
    CfaFilterTable m_table{};

    /// The validated filter arrangement.
    CfaPattern m_pattern = CfaPattern::None;

    /// The validated horizontal phase.
    std::uint32_t m_originX = 0u;

    /// The validated vertical phase.
    std::uint32_t m_originY = 0u;
};

/// The pattern's own name, for a log line, a diagnostic field or the descriptor's text form.
///
/// The spelling of the enumerator: `CfaPattern::Rggb` is `"Rggb"`.
///
/// Thread-safe and allocation-free.
///
/// @param pattern the pattern to name. A value outside the enumeration is named
///        `"UnknownCfaPattern"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* cfaPatternName(CfaPattern pattern) noexcept;

} // namespace an

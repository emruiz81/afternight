// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where an image came from and what has been done to it.
///
/// A record, and only a record. **Nothing branches on provenance.** No algorithm reads it to decide
/// what to do, and it takes no part in deciding whether a cached result is still valid. That is a
/// rule with a reason: provenance that changes behaviour has stopped being a record and become
/// hidden state, and hidden state that travels with an image is the hardest kind to find when an
/// answer comes out wrong.
///
/// What it is for is the questions a person asks afterwards. Which file is this stack made of. Was
/// this frame calibrated before it was registered, or after. Did somebody accept a lossy decode on
/// the way in - which is a real case rather than a hypothetical one, because the engine will open a
/// file whose samples cannot be carried exactly if the caller says so, and the acknowledgement has
/// to travel with the data rather than live in whoever ran the import.
///
/// It is append-only. An entry is added, never edited and never removed, because a lineage a step
/// can be taken out of is a lineage that cannot be trusted for the one question it exists to answer.
/// The number of entries is bounded, so a loop that appends per iteration fails as a refusal rather
/// than as memory growth nobody attributed to metadata.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an
{

/// Where an image's samples originally came from.
///
/// Coarse on purpose: it says what kind of origin to expect the name to describe, so a reader knows
/// whether a path, a description, or nothing at all is the right thing to look for.
enum class ProvenanceSourceKind : std::uint8_t
{
    /// Nothing is claimed about the origin.
    Unknown,

    /// The samples were decoded from a file, which the source name identifies.
    File,

    /// The samples were computed by this engine from nothing - a synthetic frame, a generated
    /// pattern, a test fixture.
    Generated,

    /// The samples were derived from other images, which the entries record.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Derived
};

/// Whether a value is one of the origins this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isProvenanceSourceKind(ProvenanceSourceKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(ProvenanceSourceKind::Derived);
}

/// The origin's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param kind the origin to name. A value outside the enumeration is named
///        `"UnknownProvenanceSourceKind"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* provenanceSourceKindName(ProvenanceSourceKind kind) noexcept;

/// How many entries one lineage may hold.
///
/// A bound rather than a limit somebody will hit: a thousand operations on one image is far beyond
/// any real workflow and far below the point at which an accidental loop would be noticed as memory
/// rather than as a refusal.
inline constexpr std::size_t kMaxProvenanceEntries = 1024u;

/// The longest text one lineage field may carry, in bytes.
///
/// Long enough for a path and a sentence, short enough that a corrupt header cannot turn a lineage
/// into an allocation.
inline constexpr std::size_t kMaxProvenanceTextBytes = 4096u;

/// One thing that was done to an image.
///
/// Two fields, and the split matters: the operation is what was done, spelled as the engine spells
/// it, and the detail is for a person. Nothing parses either of them - a caller that needs to act
/// on lineage is reading a record it should have been given as a parameter.
class ProvenanceEntry
{
public:
    /// An entry naming an operation, with optional detail.
    ///
    /// @param operation what was done. Must not be empty and must be within
    ///        `kMaxProvenanceTextBytes`.
    /// @param detail anything worth recording beside it - a parameter, an acknowledgement, a
    ///        source. May be empty; must be within `kMaxProvenanceTextBytes`.
    /// @return the entry, or `ErrorCode::InvalidArgument` naming the field at fault.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ProvenanceEntry> create(std::string operation, std::string detail);

    /// What was done.
    ///
    /// @return the operation name, never empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& operation() const noexcept;

    /// Anything worth recording beside it.
    ///
    /// @return the detail, which may be empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& detail() const noexcept;

    /// Whether two entries record the same thing.
    ///
    /// @param left one entry.
    /// @param right the other.
    /// @return true when both fields are equal.
    [[nodiscard]] friend bool operator==(const ProvenanceEntry& left, const ProvenanceEntry& right) = default;

private:
    /// Builds from text that has already been checked.
    ///
    /// @param operation what was done.
    /// @param detail anything worth recording beside it.
    ProvenanceEntry(std::string operation, std::string detail);

    /// What was done.
    std::string m_operation;

    /// Anything worth recording beside it.
    std::string m_detail;
};

/// Where an image came from and what has been done to it.
///
/// Immutable and append-only: `appended` returns a second lineage rather than changing this one, so
/// a lineage handed to something else cannot grow behind its holder's back. A default-constructed
/// one claims nothing, which is what an image assembled in memory has until something says
/// otherwise.
class Provenance
{
public:
    /// A lineage that claims nothing.
    Provenance() = default;

    /// A lineage with a stated origin and no entries yet.
    ///
    /// @param kind what kind of origin this is.
    /// @param sourceName what identifies it - a path for a file, a description for anything else.
    ///        Must be within `kMaxProvenanceTextBytes`, and must be empty when the kind is
    ///        `ProvenanceSourceKind::Unknown`, because a name beside an unknown origin is a claim
    ///        the kind contradicts.
    /// @return the lineage, or `ErrorCode::InvalidArgument` naming the field at fault.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<Provenance> create(ProvenanceSourceKind kind,
                                                                        std::string sourceName);

    /// What kind of origin this image has.
    ///
    /// @return the origin kind.
    [[nodiscard]] AFTERNIGHTENGINE_API ProvenanceSourceKind sourceKind() const noexcept;

    /// What identifies the origin.
    ///
    /// @return the name, or an empty string when nothing is claimed.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& sourceName() const noexcept;

    /// Everything that has been done to this image, oldest first.
    ///
    /// @return the entries, borrowed from this lineage and valid for as long as it is.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const ProvenanceEntry> entries() const noexcept;

    /// A second lineage with one more entry at the end.
    ///
    /// @param entry what was done.
    /// @return the extended lineage, or `ErrorCode::LimitExceeded` carrying the count and the bound
    ///         when this one is already full. This lineage is never changed.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<Provenance> appended(const ProvenanceEntry& entry) const;

    /// Whether two lineages record the same history.
    ///
    /// @param left one lineage.
    /// @param right the other.
    /// @return true when the origin and every entry are equal, in order.
    [[nodiscard]] friend bool operator==(const Provenance& left, const Provenance& right) = default;

private:
    /// What identifies the origin.
    std::string m_sourceName{};

    /// Everything that has been done, oldest first.
    std::vector<ProvenanceEntry> m_entries{};

    /// What kind of origin this image has.
    ProvenanceSourceKind m_sourceKind = ProvenanceSourceKind::Unknown;
};

} // namespace an

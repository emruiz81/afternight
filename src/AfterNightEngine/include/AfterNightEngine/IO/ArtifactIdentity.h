// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The identity a generated file carries in its own header.
///
/// A file this engine writes for a workflow is validated for reuse by what it says about itself,
/// not by hashing its payload: the artifact identity names the manifest entry the file satisfies,
/// and the run identity names the execution that produced it. The first is a reuse key and the
/// second is provenance only, which is why one is required and the other may be empty.
///
/// The alphabet is the card-safe one - letters, digits, underscore and hyphen - and the bound is
/// what one eighty-character card holds without truncation. A malformed identity is refused where
/// it is stated and refused again where it is read, and nothing repairs it in between, because
/// dropping the characters outside the alphabet could turn two different identities into one stamp.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// The keyword under which a generated file states the manifest identity it satisfies.
inline constexpr std::string_view kArtifactIdentityKeyword = "ANARTID";

/// The keyword under which a generated file states the execution that produced it.
inline constexpr std::string_view kRunIdentityKeyword = "ANRUNID";

/// The longest identity one card holds without truncation; 32 characters carry 128 bits.
inline constexpr std::size_t kMaxArtifactIdentityChars = 32u;

/// Whether one byte belongs to the identity alphabet.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param value the byte to test.
/// @return true for an ASCII letter or digit, an underscore or a hyphen.
[[nodiscard]] constexpr bool isArtifactIdentityChar(char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
           value == '_' || value == '-';
}

/// Whether a keyword is one of the two identity cards, compared exactly.
///
/// Thread-safe and allocation-free.
///
/// @param keyword the keyword to test.
/// @return true for the artifact or the run identity keyword.
[[nodiscard]] AFTERNIGHTENGINE_API bool isArtifactIdentityKeyword(std::string_view keyword) noexcept;

/// What a generated file says about which manifest entry it satisfies and which run wrote it.
///
/// Immutable; `create` is the only way in and it refuses an identity a card cannot hold.
class ArtifactIdentity
{
public:
    /// An identity from its two parts.
    ///
    /// @param artifactId the manifest identity. Nonempty, at most `kMaxArtifactIdentityChars`
    ///        bytes, every one of them accepted by `isArtifactIdentityChar`.
    /// @param runId the producing execution, or empty when none is recorded. Bounded and
    ///        restricted the same way when stated.
    /// @return the identity, or `ErrorCode::InvalidArgument` with `CodecFailure::InvalidIdentity`
    ///         naming the part at fault.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<ArtifactIdentity> create(std::string artifactId,
                                                                              std::string runId);

    /// The manifest identity this file satisfies.
    ///
    /// @return the artifact identity, never empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& artifactId() const noexcept;

    /// The execution that produced the file.
    ///
    /// @return the run identity, which may be empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& runId() const noexcept;

    /// Whether two identities name the same artifact and run.
    ///
    /// @param left one identity.
    /// @param right the other.
    /// @return true when both parts are equal.
    [[nodiscard]] friend bool operator==(const ArtifactIdentity& left, const ArtifactIdentity& right) = default;

private:
    /// Builds from parts that have already been checked.
    ///
    /// @param artifactId the manifest identity.
    /// @param runId the producing execution.
    ArtifactIdentity(std::string artifactId, std::string runId);

    /// The manifest identity.
    std::string m_artifactId;

    /// The producing execution.
    std::string m_runId;
};

/// The identity a preserved card set carries, read without repair.
///
/// Both halves of the lookup are used: the namespace names which format's card is consulted, so a
/// FITS stamp is not read out of an XISF property. A value may be spelled as the file wrote it -
/// surrounded by whitespace, or by one pair of single quotes with the blanks a FITS string pads its
/// value with inside them - and is otherwise taken exactly.
///
/// Allocates the returned identity only.
///
/// @param codec the preserved cards.
/// @param origin which format's vocabulary to consult.
/// @return the identity; an empty optional when no artifact card is present, whether or not a run
///         card is; or `ErrorCode::InvalidData` with `CodecFailure::InvalidIdentity` when a card
///         is present and its value is not an identity.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::optional<ArtifactIdentity>>
artifactIdentityOf(const CodecMetadata& codec, CodecNamespace origin);

/// The cards that state an identity in a destination's vocabulary.
///
/// One card for the artifact identity and, when the run identity is stated, one for it, each with
/// a comment saying what it is. The value is the identity itself, unquoted; a codec that quotes
/// string values does so when it writes.
///
/// @param identity what to state.
/// @param origin the namespace the destination's cards belong to.
/// @return the cards, in that order, or `ErrorCode::InvalidArgument` for a namespace outside the
///         enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<CodecCard>>
artifactIdentityCards(const ArtifactIdentity& identity, CodecNamespace origin);

} // namespace an

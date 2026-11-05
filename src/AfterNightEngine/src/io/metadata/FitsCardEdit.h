// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - editing a card set's FITS cards by one rule.
//
// Two card families this engine writes on a caller's behalf - the observing context a product carries
// and the processing state it records - replace, append and remove FITS keywords by one rule: a
// keyword's first card is rewritten where it stands and keeps its comment, later cards of the keyword
// go, and a keyword that is not there is appended. Keeping that rule in one place
// is what keeps the two families from disagreeing about where a card ends up.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an
{

/// A card set's cards while FITS keywords are written onto and removed from them.
///
/// Writing a keyword replaces the value of its first `Fits` card in place, keeping that card's comment,
/// and removes the keyword's later `Fits` cards; a keyword the set lacks is appended with no comment.
/// Removing a keyword removes every `Fits` card that carries it. Cards of other namespaces are never
/// touched. The first refusal is kept and every later edit does nothing, so a caller states its edits
/// in sequence and asks once, at the end, whether they held.
class FitsCardEdit
{
public:
    /// Starts from a card set.
    ///
    /// @param codec the cards and source encoding to edit a copy of.
    /// @param operation the public operation a refusal names, as a string literal.
    FitsCardEdit(const CodecMetadata& codec, const char* operation);

    /// The first `Fits` card of a keyword, as the edits so far left it.
    ///
    /// @param keyword the keyword to find.
    /// @return the card, or null. Borrowed until the next edit.
    [[nodiscard]] const CodecCard* first(std::string_view keyword) const noexcept;

    /// Writes a keyword's value.
    ///
    /// Refused as `ErrorCode::LimitExceeded` when the value is longer than one card holds, or when the
    /// keyword is new and the set already holds as many cards as one image preserves; an empty or
    /// overlong keyword is refused by the card itself as `ErrorCode::InvalidArgument`. A refusal is
    /// recorded rather than returned, and a write after one does nothing.
    ///
    /// @param keyword the keyword.
    /// @param value its value, as a card spells it.
    void write(std::string_view keyword, std::string value);

    /// Removes every `Fits` card of a keyword.
    ///
    /// @param keyword the keyword.
    void remove(std::string_view keyword);

    /// Removes every `Fits` card whose keyword a predicate accepts.
    ///
    /// @tparam PredicateT callable with a `std::string_view` keyword, returning whether to remove it.
    /// @param matches the predicate.
    template <typename PredicateT> void removeWhere(PredicateT&& matches)
    {
        if (m_failure.failed())
            return;
        std::erase_if(m_cards, [&matches](const CodecCard& card) {
            return card.origin() == CodecNamespace::Fits && matches(std::string_view(card.keyword()));
        });
    }

    /// Records a refusal found outside the edits, unless one is recorded already.
    ///
    /// @param failure the refusal.
    void fail(Status failure);

    /// Whether an edit has been refused.
    ///
    /// @return true once a refusal is recorded.
    [[nodiscard]] bool failed() const noexcept;

    /// The edited cards.
    ///
    /// @return the cards with the source encoding they started with, or the first refusal.
    [[nodiscard]] Result<CodecMetadata> finish() &&;

private:
    /// The cards being edited.
    std::vector<CodecCard> m_cards;

    /// The source encoding, carried through unchanged.
    SourceEncoding m_encoding;

    /// The first refusal, or success.
    Status m_failure{};

    /// The public operation a refusal names.
    const char* m_operation;
};

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a field longer than one card, continued in the cards after it.
//
// One card holds `kMaxCodecCardTextBytes` bytes of text and a container's field can hold far more: an embedded colour
// profile, a camera's thumbnail, the matrices of an astrometric solution, a long processing history. Splitting the
// text across consecutive cards of the field's keyword is what FITS does with a long string, and it keeps every such
// field inspectable rather than opaque. The containers share this one rule, so what any of them splits, every reader
// of cards joins the same way.

#pragma once

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <string>
#include <string_view>
#include <vector>

namespace an
{
/// The comment a card carries when it continues the value of the card before it.
inline constexpr std::string_view kContinuedCardComment = "continued";

/// Whether a card continues the value of the card before it: it carries `kContinuedCardComment`, under the same
/// namespace and keyword.
///
/// Thread-safe and allocation-free.
///
/// @param previous the card before it.
/// @param card the card to test.
/// @return true when `card` is a continuation of `previous`.
[[nodiscard]] bool continuesCard(const CodecCard& previous, const CodecCard& card) noexcept;

/// Appends one field as cards, continuing into further cards when its text does not fit one.
///
/// Each card holds at most `kMaxCodecCardTextBytes` bytes and never splits a UTF-8 character across two cards; an
/// empty value is one empty card. The first card carries the stated comment and each further card
/// `kContinuedCardComment`; a reader joins them in order.
///
/// @param cards where to append.
/// @param origin the namespace the container's cards belong to.
/// @param keyword the field's key.
/// @param text the field's complete value, which may be empty.
/// @param comment what to write beside the first card.
/// @param operation what the caller was doing, named in a refusal.
/// @return success; `ErrorCode::LimitExceeded` when the list would hold more than `kMaxCodecCards` cards, naming the
///         keyword and the bound; or whatever building a card reported. Cards appended before a failure stay in the
///         list.
[[nodiscard]] Status appendFieldCards(std::vector<CodecCard>& cards, CodecNamespace origin, const std::string& keyword,
                                      std::string_view text, const std::string& comment, std::string_view operation);
} // namespace an

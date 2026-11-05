// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a field longer than one card, continued in the cards after it.

#include "image/CardContinuation.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace an
{
bool continuesCard(const CodecCard& previous, const CodecCard& card) noexcept
{
    return card.comment() == kContinuedCardComment && card.origin() == previous.origin() &&
           card.keyword() == previous.keyword();
}

Status appendFieldCards(std::vector<CodecCard>& cards, const CodecNamespace origin, const std::string& keyword,
                        const std::string_view text, const std::string& comment, const std::string_view operation)
{
    std::size_t offset = 0u;
    do
    {
        std::size_t length = (std::min)(kMaxCodecCardTextBytes, text.size() - offset);
        if (offset + length < text.size())
        {
            // A character is never split across two cards: a continuation byte belongs with the
            // byte that introduced it.
            while (length > 0u && (static_cast<unsigned char>(text[offset + length]) & 0xc0u) == 0x80u)
            {
                --length;
            }
            if (length == 0u)
            {
                length = (std::min)(kMaxCodecCardTextBytes, text.size() - offset);
            }
        }
        if (cards.size() >= kMaxCodecCards)
        {
            return Status::failure(ErrorCode::LimitExceeded,
                                   {.message = "the source states more metadata than one image preserves",
                                    .operation = std::string(operation),
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, keyword),
                                                StatusContextField::ofCount(
                                                    ContextKey::Limit, static_cast<std::uint64_t>(kMaxCodecCards))}});
        }
        auto card = CodecCard::create(origin, keyword, std::string(text.substr(offset, length)),
                                      offset == 0u ? comment : std::string(kContinuedCardComment));
        if (!card.hasValue())
        {
            return card.status();
        }
        cards.push_back(std::move(*card));
        offset += length;
    } while (offset < text.size());
    return {};
}
} // namespace an

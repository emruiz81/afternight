// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FITS card edit rule shared by the observing-context and processing-state cards.
//
// Every edit works on a private copy of the cards and becomes a card set only at `finish`, so a
// refused edit leaves the caller's cards as they were. A replacement is built as a new card and
// assigned over the first one, keeping its position and comment, and the later cards of the same
// keyword are removed after it, never before.

#include "io/metadata/FitsCardEdit.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace an
{
namespace
{
Status limitFailure(const char* operation, const char* message, std::string_view keyword, std::uint64_t requested,
                    std::uint64_t limit)
{
    return Status::failure(ErrorCode::LimitExceeded,
                           {.message = message,
                            .operation = operation,
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, std::string(keyword)),
                                        StatusContextField::ofCount(ContextKey::Requested, requested),
                                        StatusContextField::ofCount(ContextKey::Limit, limit)}});
}

bool fitsKeyword(const CodecCard& card, std::string_view keyword) noexcept
{
    return card.origin() == CodecNamespace::Fits && card.keyword() == keyword;
}
} // namespace

FitsCardEdit::FitsCardEdit(const CodecMetadata& codec, const char* operation)
    : m_cards(codec.cards().begin(), codec.cards().end()), m_encoding(codec.sourceEncoding()), m_operation(operation)
{
}

const CodecCard* FitsCardEdit::first(std::string_view keyword) const noexcept
{
    const auto found = std::find_if(m_cards.begin(), m_cards.end(),
                                    [keyword](const CodecCard& card) { return fitsKeyword(card, keyword); });
    return found != m_cards.end() ? &*found : nullptr;
}

void FitsCardEdit::write(std::string_view keyword, std::string value)
{
    if (m_failure.failed())
        return;
    if (value.size() > kMaxCodecCardTextBytes)
    {
        m_failure = limitFailure(m_operation, "a written value is longer than one card holds", keyword, value.size(),
                                 kMaxCodecCardTextBytes);
        return;
    }
    const auto sameKeyword = [keyword](const CodecCard& card) { return fitsKeyword(card, keyword); };
    const auto first = std::find_if(m_cards.begin(), m_cards.end(), sameKeyword);
    if (first == m_cards.end())
    {
        if (m_cards.size() >= kMaxCodecCards)
        {
            m_failure = limitFailure(m_operation, "the written cards are more than one image preserves", keyword,
                                     m_cards.size() + 1u, kMaxCodecCards);
            return;
        }
        Result<CodecCard> card = CodecCard::create(CodecNamespace::Fits, std::string(keyword), std::move(value), {});
        if (!card.hasValue())
        {
            m_failure = card.status();
            return;
        }
        m_cards.push_back(std::move(*card));
        return;
    }
    Result<CodecCard> card =
        CodecCard::create(CodecNamespace::Fits, first->keyword(), std::move(value), first->comment());
    if (!card.hasValue())
    {
        m_failure = card.status();
        return;
    }
    *first = std::move(*card);
    m_cards.erase(std::remove_if(std::next(first), m_cards.end(), sameKeyword), m_cards.end());
}

void FitsCardEdit::remove(std::string_view keyword)
{
    if (m_failure.failed())
        return;
    std::erase_if(m_cards, [keyword](const CodecCard& card) { return fitsKeyword(card, keyword); });
}

void FitsCardEdit::fail(Status failure)
{
    if (!m_failure.failed())
        m_failure = std::move(failure);
}

bool FitsCardEdit::failed() const noexcept
{
    return m_failure.failed();
}

Result<CodecMetadata> FitsCardEdit::finish() &&
{
    if (m_failure.failed())
        return m_failure;
    return CodecMetadata::create(m_encoding, std::move(m_cards));
}

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/CodecMetadata.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The lookup is linear and that is a decision rather than an oversight.
//
// A header is tens of cards in every real file, a map would cost an allocation and a hash per card
// on a path that runs once per decode, and the order of the cards is itself information a round
// trip preserves - so the container has to be a sequence whatever the lookup does. A codec that
// finds itself searching a large header in a loop is a codec that should read what it needs once,
// which is what the typed fields beside these cards are for.
//
// The scale check refuses zero as well as the non-finite values. A stored value multiplied by zero
// is the same number whatever it was, so an encoding that claims it is an encoding that cannot be
// undone, and preserving it would let a round trip write back a frame of one value.

namespace an
{
namespace
{

/// Whether a stated number is one an encoding can be undone with.
///
/// @param value the number to test.
/// @return true when it is finite.
[[nodiscard]] bool isFinite(double value) noexcept
{
    return value > -std::numeric_limits<double>::infinity() && value < std::numeric_limits<double>::infinity();
}

/// The refusal for a field that cannot be what it claims.
///
/// @param message what is wrong, for a person.
/// @param parameter which field, spelled as the caller wrote it.
/// @param operation the engine operation to name in the failure.
/// @return an `ErrorCode::InvalidArgument` failure naming the field.
[[nodiscard]] Status refuseField(const char* message, const char* parameter, const char* operation)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// The refusal for more cards than one image preserves.
///
/// @param held how many are already there.
/// @param operation the engine operation to name in the failure.
/// @return an `ErrorCode::LimitExceeded` failure carrying the count and the bound.
[[nodiscard]] Status refuseCardCount(std::size_t held, const char* operation)
{
    return Status::failure(
        ErrorCode::LimitExceeded,
        StatusDetail{
            .message = "more cards than one image preserves",
            .operation = operation,
            .domain = ErrorDomain::Image,
            .context = {StatusContextField::ofCount(ContextKey::Requested, static_cast<std::uint64_t>(held)),
                        StatusContextField::ofCount(ContextKey::Limit, static_cast<std::uint64_t>(kMaxCodecCards))}});
}

} // namespace

const char* codecNamespaceName(CodecNamespace codecNamespace) noexcept
{
    switch (codecNamespace)
    {
    case CodecNamespace::Unspecified:
        return "Unspecified";
    case CodecNamespace::Fits:
        return "Fits";
    case CodecNamespace::Tiff:
        return "Tiff";
    case CodecNamespace::Png:
        return "Png";
    case CodecNamespace::Jpeg:
        return "Jpeg";
    case CodecNamespace::Xisf:
        return "Xisf";
    case CodecNamespace::CameraRaw:
        return "CameraRaw";
    case CodecNamespace::Ser:
        return "Ser";
    case CodecNamespace::Ffmpeg:
        return "Ffmpeg";
    }
    return "UnknownCodecNamespace";
}

CodecCard::CodecCard(CodecNamespace origin, std::string keyword, std::string value, std::string comment)
    : m_keyword(std::move(keyword)), m_value(std::move(value)), m_comment(std::move(comment)), m_origin(origin)
{
}

Result<CodecCard> CodecCard::create(CodecNamespace origin, std::string keyword, std::string value, std::string comment)
{
    if (!isCodecNamespace(origin))
    {
        return refuseField("the card namespace is not one this engine knows", "origin", "CodecCard::create");
    }
    if (keyword.empty())
    {
        return refuseField("a card without a keyword cannot be found again", "keyword", "CodecCard::create");
    }
    if (keyword.size() > kMaxCodecCardTextBytes)
    {
        return refuseField("the keyword is longer than a card carries", "keyword", "CodecCard::create");
    }
    if (value.size() > kMaxCodecCardTextBytes)
    {
        return refuseField("the value is longer than a card carries", "value", "CodecCard::create");
    }
    if (comment.size() > kMaxCodecCardTextBytes)
    {
        return refuseField("the comment is longer than a card carries", "comment", "CodecCard::create");
    }
    return CodecCard{origin, std::move(keyword), std::move(value), std::move(comment)};
}

CodecNamespace CodecCard::origin() const noexcept
{
    return m_origin;
}

const std::string& CodecCard::keyword() const noexcept
{
    return m_keyword;
}

const std::string& CodecCard::value() const noexcept
{
    return m_value;
}

const std::string& CodecCard::comment() const noexcept
{
    return m_comment;
}

Result<CodecMetadata> CodecMetadata::create(const SourceEncoding& encoding, std::vector<CodecCard> cards)
{
    if (encoding.sampleFormat.has_value() && !isRawSampleFormat(*encoding.sampleFormat))
    {
        return refuseField("the source sample format is not one this engine describes", "sampleFormat",
                           "CodecMetadata::create");
    }
    if (encoding.byteOrder.has_value() && !isByteOrder(*encoding.byteOrder))
    {
        return refuseField("the source byte order is not one this engine reads", "byteOrder", "CodecMetadata::create");
    }
    if (encoding.scale.has_value() && (!isFinite(*encoding.scale) || *encoding.scale == 0.0))
    {
        return refuseField("a source scale of zero or of no value cannot be undone", "scale", "CodecMetadata::create");
    }
    if (encoding.zeroOffset.has_value() && !isFinite(*encoding.zeroOffset))
    {
        return refuseField("a source offset is a finite number", "zeroOffset", "CodecMetadata::create");
    }
    if (cards.size() > kMaxCodecCards)
    {
        return refuseCardCount(cards.size(), "CodecMetadata::create");
    }

    CodecMetadata metadata;
    metadata.m_sourceEncoding = encoding;
    metadata.m_cards = std::move(cards);
    return metadata;
}

const SourceEncoding& CodecMetadata::sourceEncoding() const noexcept
{
    return m_sourceEncoding;
}

std::span<const CodecCard> CodecMetadata::cards() const noexcept
{
    return std::span<const CodecCard>{m_cards};
}

const CodecCard* CodecMetadata::find(CodecNamespace origin, std::string_view keyword) const noexcept
{
    for (const CodecCard& card : m_cards)
    {
        if (card.origin() == origin && card.keyword() == keyword)
        {
            return &card;
        }
    }
    return nullptr;
}

Result<CodecMetadata> CodecMetadata::appended(const CodecCard& card) const
{
    if (m_cards.size() >= kMaxCodecCards)
    {
        return refuseCardCount(m_cards.size(), "CodecMetadata::appended");
    }
    CodecMetadata extended = *this;
    extended.m_cards.push_back(card);
    return extended;
}

} // namespace an

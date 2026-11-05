// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a frame's kind read from the cards its codec preserved.
//
// The words are searched in a fixed order - bias and zero before dark, dark before flat, flat before light and
// science - and `OFFSET` names a bias anywhere in a value rather than only as the whole of one. The search is
// ASCII-case-insensitive and over the value as written, quotes and padding included, because a word inside
// quotes is still the word.

#include <AfterNightEngine/IO/FrameTypeCards.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace an
{
namespace
{
using std::string_view_literals::operator""sv;

/// The keywords consulted, in order; each index is one less than its `FrameTypeCard`.
constexpr std::array<std::string_view, 3u> kKeywords = {"IMAGETYP"sv, "FRAME"sv, "ImageType"sv};

/// The namespace each keyword is read in, in the same order.
constexpr std::array<CodecNamespace, 3u> kNamespaces = {CodecNamespace::Fits, CodecNamespace::Fits,
                                                        CodecNamespace::Xisf};

/// One word and the kind it names.
struct KindWord
{
    std::string_view word;
    FrameType kind;
};

/// Every word in the order it is searched for; the first that appears decides.
constexpr std::array<KindWord, 7u> kWords = {
    KindWord{"BIAS"sv, FrameType::Bias},    KindWord{"ZERO"sv, FrameType::Bias}, KindWord{"OFFSET"sv, FrameType::Bias},
    KindWord{"DARK"sv, FrameType::Dark},    KindWord{"FLAT"sv, FrameType::Flat}, KindWord{"LIGHT"sv, FrameType::Light},
    KindWord{"SCIENCE"sv, FrameType::Light}};

/// Whether a value holds an upper-case word, comparing without regard to ASCII case.
[[nodiscard]] bool holds(std::string_view text, std::string_view upperWord) noexcept
{
    return std::search(text.begin(), text.end(), upperWord.begin(), upperWord.end(), [](char value, char upper) {
               if (value >= 'a' && value <= 'z')
                   value = static_cast<char>(value - ('a' - 'A'));
               return value == upper;
           }) != text.end();
}

/// The first card that names a kind, or none.
[[nodiscard]] const CodecCard* decidingCard(const CodecMetadata& codec, FrameTypeCard& which, FrameType& kind) noexcept
{
    for (std::size_t index = 0u; index < kKeywords.size(); ++index)
    {
        const CodecCard* const card = codec.find(kNamespaces[index], kKeywords[index]);
        if (card == nullptr)
            continue;
        const FrameType named = classifyFrameTypeText(card->value());
        // The XISF attribute defaults to Light, and the codec preserves a default as it would a stated value.
        const bool xisfDefault = kNamespaces[index] == CodecNamespace::Xisf && named == FrameType::Light;
        if (named == FrameType::Unknown || xisfDefault)
            continue;
        which = static_cast<FrameTypeCard>(index + 1u);
        kind = named;
        return card;
    }
    return nullptr;
}
} // namespace

const char* frameTypeCardName(FrameTypeCard card) noexcept
{
    switch (card)
    {
    case FrameTypeCard::None:
        return "None";
    case FrameTypeCard::ImageType:
        return "ImageType";
    case FrameTypeCard::Frame:
        return "Frame";
    case FrameTypeCard::XisfImageType:
        return "XisfImageType";
    }
    return "UnknownFrameTypeCard";
}

std::span<const std::string_view> frameTypeKeywords() noexcept
{
    return kKeywords;
}

FrameType classifyFrameTypeText(std::string_view text) noexcept
{
    for (const KindWord& entry : kWords)
        if (holds(text, entry.word))
            return entry.kind;
    return FrameType::Unknown;
}

FrameTypeReading readFrameTypeCards(const CodecMetadata& codec)
{
    FrameTypeReading reading;
    if (const CodecCard* const card = decidingCard(codec, reading.card, reading.type); card != nullptr)
        reading.spelling = card->value();
    return reading;
}

FrameType resolveFrameType(const CodecMetadata& codec, FrameType fallback) noexcept
{
    FrameTypeCard which = FrameTypeCard::None;
    FrameType kind = FrameType::Unknown;
    return decidingCard(codec, which, kind) != nullptr ? kind : fallback;
}

} // namespace an

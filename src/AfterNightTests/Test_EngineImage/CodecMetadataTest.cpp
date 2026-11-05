// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/CodecMetadata.h.
//
// Two properties are what make preserved cards safe to keep rather than a second source of truth.
//
// **A card is namespaced, and the lookup matches both halves.** Two formats can spell one keyword,
// so a search that ignored the origin would find whichever card was read first - which is exactly
// how a preserved value ends up being read as an engine fact.
//
// **The source encoding is source facts, typed.** How wide the file's samples were, in which order,
// with what scale and offset, is kept here and nowhere else. The output side of the engine has its
// own object, and the case below checks that these fields do not travel by being the same shape.

#include <AfterNightEngine/Image/CodecMetadata.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// A card that is valid, for cases that are about something else.
///
/// @param origin which format the keyword belongs to.
/// @param keyword the key.
/// @param value what the file said.
/// @return the card.
[[nodiscard]] an::CodecCard makeCard(an::CodecNamespace origin, std::string keyword, std::string value)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(origin, std::move(keyword), std::move(value), "");
    EXPECT_TRUE(card.hasValue());
    return card.hasValue() ? std::move(*card) : an::CodecCard::create(origin, "FALLBACK", "", "").value();
}

/// The source encoding a 16-bit FITS frame with an offset arrives with.
///
/// @return the encoding.
[[nodiscard]] an::SourceEncoding sixteenBitFits()
{
    return an::SourceEncoding{.sampleFormat = an::rawSampleFormatOf(an::SampleType::Int16),
                              .byteOrder = an::ByteOrder::Big,
                              .scale = 1.0,
                              .zeroOffset = 32768.0};
}

TEST(CodecMetadataTest, TheDefaultPreservesNothingAndClaimsNothing)
{
    const an::CodecMetadata metadata;
    EXPECT_TRUE(metadata.cards().empty());
    EXPECT_FALSE(metadata.sourceEncoding().sampleFormat.has_value());
    EXPECT_FALSE(metadata.sourceEncoding().byteOrder.has_value());
    EXPECT_FALSE(metadata.sourceEncoding().scale.has_value());
    EXPECT_FALSE(metadata.sourceEncoding().zeroOffset.has_value());
    EXPECT_EQ(metadata.find(an::CodecNamespace::Fits, "ANYTHING"), nullptr);
}

TEST(CodecMetadataTest, TheSourceEncodingIsCarriedAsTypedSourceFacts)
{
    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(sixteenBitFits(), {});
    ASSERT_TRUE(metadata.hasValue()) << metadata.status().message();

    const an::SourceEncoding& encoding = metadata->sourceEncoding();
    ASSERT_TRUE(encoding.sampleFormat.has_value());
    EXPECT_EQ(*encoding.sampleFormat, an::rawSampleFormatOf(an::SampleType::Int16));
    ASSERT_TRUE(encoding.byteOrder.has_value());
    EXPECT_EQ(*encoding.byteOrder, an::ByteOrder::Big);
    EXPECT_DOUBLE_EQ(*encoding.scale, 1.0);
    EXPECT_DOUBLE_EQ(*encoding.zeroOffset, 32768.0);
}

TEST(CodecMetadataTest, CardsAreKeptInTheOrderTheyWereRead)
{
    std::vector<an::CodecCard> cards;
    cards.push_back(makeCard(an::CodecNamespace::Fits, "SIMPLE", "T"));
    cards.push_back(makeCard(an::CodecNamespace::Fits, "OBSERVER", "someone"));
    cards.push_back(makeCard(an::CodecNamespace::Fits, "SITELONG", "-58.4"));

    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    ASSERT_TRUE(metadata.hasValue()) << metadata.status().message();

    const std::span<const an::CodecCard> kept = metadata->cards();
    ASSERT_EQ(kept.size(), 3u);
    EXPECT_EQ(kept[0].keyword(), "SIMPLE");
    EXPECT_EQ(kept[1].keyword(), "OBSERVER");
    EXPECT_EQ(kept[2].keyword(), "SITELONG");
}

TEST(CodecMetadataTest, TheLookupMatchesTheNamespaceAsWellAsTheKeyword)
{
    std::vector<an::CodecCard> cards;
    cards.push_back(makeCard(an::CodecNamespace::Fits, "SOFTWARE", "from the header"));
    cards.push_back(makeCard(an::CodecNamespace::Tiff, "SOFTWARE", "from the tag"));

    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    ASSERT_TRUE(metadata.hasValue()) << metadata.status().message();

    const an::CodecCard* const fits = metadata->find(an::CodecNamespace::Fits, "SOFTWARE");
    ASSERT_NE(fits, nullptr);
    EXPECT_EQ(fits->value(), "from the header");

    const an::CodecCard* const tiff = metadata->find(an::CodecNamespace::Tiff, "SOFTWARE");
    ASSERT_NE(tiff, nullptr);
    EXPECT_EQ(tiff->value(), "from the tag");

    EXPECT_EQ(metadata->find(an::CodecNamespace::Png, "SOFTWARE"), nullptr);
}

TEST(CodecMetadataTest, TheKeywordComparisonIsExactAndNotCaseFolded)
{
    std::vector<an::CodecCard> cards;
    cards.push_back(makeCard(an::CodecNamespace::Fits, "EXPTIME", "300"));

    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    ASSERT_TRUE(metadata.hasValue());

    EXPECT_NE(metadata->find(an::CodecNamespace::Fits, "EXPTIME"), nullptr);
    EXPECT_EQ(metadata->find(an::CodecNamespace::Fits, "exptime"), nullptr);
}

TEST(CodecMetadataTest, TheFirstCardOfARepeatedKeywordWins)
{
    std::vector<an::CodecCard> cards;
    cards.push_back(makeCard(an::CodecNamespace::Fits, "COMMENT", "first"));
    cards.push_back(makeCard(an::CodecNamespace::Fits, "COMMENT", "second"));

    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    ASSERT_TRUE(metadata.hasValue());

    const an::CodecCard* const found = metadata->find(an::CodecNamespace::Fits, "COMMENT");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value(), "first");
}

TEST(CodecMetadataTest, AppendingProducesASecondObjectAndLeavesTheFirstAlone)
{
    an::Result<an::CodecMetadata> original = an::CodecMetadata::create(an::SourceEncoding{}, {});
    ASSERT_TRUE(original.hasValue());

    an::Result<an::CodecMetadata> extended =
        original->appended(makeCard(an::CodecNamespace::Fits, "ANARTID", "run-42"));
    ASSERT_TRUE(extended.hasValue()) << extended.status().message();

    EXPECT_TRUE(original->cards().empty());
    ASSERT_EQ(extended->cards().size(), 1u);
    EXPECT_EQ(extended->cards()[0].keyword(), "ANARTID");
}

TEST(CodecMetadataTest, ACardWithoutAKeywordCannotBeFoundAgainAndIsRefused)
{
    an::Result<an::CodecCard> refused = an::CodecCard::create(an::CodecNamespace::Fits, "", "value", "");
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);

    an::Result<an::CodecCard> emptyValue = an::CodecCard::create(an::CodecNamespace::Fits, "END", "", "");
    ASSERT_TRUE(emptyValue.hasValue()) << emptyValue.status().message();
    EXPECT_TRUE(emptyValue->value().empty());
}

TEST(CodecMetadataTest, ANamespaceOutsideTheEnumerationIsRefused)
{
    an::Result<an::CodecCard> refused = an::CodecCard::create(static_cast<an::CodecNamespace>(200), "KEY", "value", "");
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(CodecMetadataTest, TextLongerThanACardCarriesIsRefusedRatherThanTruncated)
{
    const std::string tooLong(an::kMaxCodecCardTextBytes + 1u, 'x');
    EXPECT_FALSE(an::CodecCard::create(an::CodecNamespace::Fits, tooLong, "", "").hasValue());
    EXPECT_FALSE(an::CodecCard::create(an::CodecNamespace::Fits, "KEY", tooLong, "").hasValue());
    EXPECT_FALSE(an::CodecCard::create(an::CodecNamespace::Fits, "KEY", "", tooLong).hasValue());
}

TEST(CodecMetadataTest, ASourceEncodingThatCannotBeUndoneIsRefused)
{
    an::SourceEncoding zeroScale;
    zeroScale.scale = 0.0;
    an::Result<an::CodecMetadata> refusedScale = an::CodecMetadata::create(zeroScale, {});
    ASSERT_FALSE(refusedScale.hasValue());
    EXPECT_EQ(refusedScale.code(), an::ErrorCode::InvalidArgument);

    an::SourceEncoding infiniteOffset;
    infiniteOffset.zeroOffset = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(an::CodecMetadata::create(infiniteOffset, {}).hasValue());

    an::SourceEncoding unknownFormat;
    unknownFormat.sampleFormat = an::RawSampleFormat{.sizeBytes = 3u, .sampleClass = an::RawSampleClass::Opaque};
    EXPECT_FALSE(an::CodecMetadata::create(unknownFormat, {}).hasValue());

    an::SourceEncoding unknownOrder;
    unknownOrder.byteOrder = static_cast<an::ByteOrder>(200);
    EXPECT_FALSE(an::CodecMetadata::create(unknownOrder, {}).hasValue());
}

TEST(CodecMetadataTest, MoreCardsThanOneImagePreservesIsRefusedWithTheNumbers)
{
    std::vector<an::CodecCard> cards;
    cards.reserve(an::kMaxCodecCards + 1u);
    const an::CodecCard card = makeCard(an::CodecNamespace::Fits, "COMMENT", "x");
    for (std::size_t index = 0u; index <= an::kMaxCodecCards; ++index)
    {
        cards.push_back(card);
    }

    an::Result<an::CodecMetadata> refused = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);

    const an::StatusContextField* const limit = refused.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->count, an::kMaxCodecCards);
}

TEST(CodecMetadataTest, AForeignRepresentationIsPreservableAsASourceFact)
{
    // A 64-bit integer file is describable at the boundary and is not a resident representation, so
    // its source encoding has to be able to say so rather than nearest-matching one of the eight.
    an::SourceEncoding encoding;
    encoding.sampleFormat = an::RawSampleFormat{.sizeBytes = 8u, .sampleClass = an::RawSampleClass::SignedInteger};
    encoding.byteOrder = an::ByteOrder::Big;

    an::Result<an::CodecMetadata> metadata = an::CodecMetadata::create(encoding, {});
    ASSERT_TRUE(metadata.hasValue()) << metadata.status().message();
    ASSERT_TRUE(metadata->sourceEncoding().sampleFormat.has_value());
    EXPECT_FALSE(an::residentSampleTypeOf(*metadata->sourceEncoding().sampleFormat).has_value());
}

TEST(CodecMetadataTest, EveryNamespaceHasItsOwnNameAndAStrangerSaysSo)
{
    constexpr an::CodecNamespace kAll[] = {
        an::CodecNamespace::Unspecified, an::CodecNamespace::Fits, an::CodecNamespace::Tiff,
        an::CodecNamespace::Png,         an::CodecNamespace::Jpeg, an::CodecNamespace::Xisf,
        an::CodecNamespace::CameraRaw,   an::CodecNamespace::Ser,  an::CodecNamespace::Ffmpeg};
    for (const an::CodecNamespace left : kAll)
    {
        EXPECT_TRUE(an::isCodecNamespace(left));
        for (const an::CodecNamespace right : kAll)
        {
            const bool sameName = std::strcmp(an::codecNamespaceName(left), an::codecNamespaceName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_FALSE(an::isCodecNamespace(static_cast<an::CodecNamespace>(200)));
    EXPECT_STREQ(an::codecNamespaceName(static_cast<an::CodecNamespace>(200)), "UnknownCodecNamespace");
}

} // namespace

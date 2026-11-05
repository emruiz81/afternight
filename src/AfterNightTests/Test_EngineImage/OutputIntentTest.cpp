// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/OutputIntent.h.
//
// This type exists because of a bug with a name: a FITS write that copies the source header forward
// makes a processed frame carry its input's identity. The general form is a source fact
// masquerading as an output decision, and these cases check the three ways this object refuses to
// let that happen.
//
// **The decision is complete or it is refused.** An explicit scaling without both of its numbers
// cannot be undone by a reader, and a scale carried beside a scaling that does not use one is a
// number waiting to be applied by something that assumes it does.
//
// **The cards are stated up front.** Header space is reserved before the pixels are written, so the
// set of cards has to be known before the write begins; carrying them here rather than collecting
// them as the write proceeds is what makes that possible.
//
// **A rounding rule describes quantization.** Asking for one into a floating destination is an
// author expecting something that will not happen, so it is refused rather than ignored.

#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

/// An intent request that is valid before a case spoils one field of it.
///
/// @return the request: sixteen-bit output with no scaling.
[[nodiscard]] an::OutputIntentRequest sixteenBitRequest()
{
    an::OutputIntentRequest request{.sampleType = an::SampleType::UInt16};
    request.byteOrder = an::ByteOrder::Big;
    return request;
}

TEST(OutputIntentTest, ADecisionIsCarriedCompleteAndReadsBackUnchanged)
{
    an::OutputIntentRequest request = sixteenBitRequest();
    request.scaling = an::OutputScaling::Explicit;
    request.scale = 1.0;
    request.zeroOffset = 32768.0;
    request.rounding = an::OutputRounding::Nearest;
    request.compression = an::OutputCompression::Rice;
    request.preserveSourceCards = false;
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "ANARTID", "run-42", "");
    ASSERT_TRUE(card.hasValue());
    request.cards.push_back(*card);

    an::Result<an::OutputIntent> intent = an::OutputIntent::create(request);
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();

    EXPECT_EQ(intent->sampleType(), an::SampleType::UInt16);
    EXPECT_EQ(intent->byteOrder(), an::ByteOrder::Big);
    EXPECT_EQ(intent->scaling(), an::OutputScaling::Explicit);
    EXPECT_DOUBLE_EQ(*intent->scale(), 1.0);
    EXPECT_DOUBLE_EQ(*intent->zeroOffset(), 32768.0);
    EXPECT_EQ(intent->rounding(), an::OutputRounding::Nearest);
    EXPECT_EQ(intent->compression(), an::OutputCompression::Rice);
    EXPECT_FALSE(intent->preserveSourceCards());
    ASSERT_EQ(intent->cards().size(), 1u);
    EXPECT_EQ(intent->cards()[0].keyword(), "ANARTID");
}

TEST(OutputIntentTest, TheIntentRoundTripsThroughItsRequest)
{
    an::OutputIntentRequest request = sixteenBitRequest();
    request.scaling = an::OutputScaling::NormalizeFullRange;
    request.rounding = an::OutputRounding::Floor;

    an::Result<an::OutputIntent> intent = an::OutputIntent::create(request);
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();

    an::Result<an::OutputIntent> rebuilt = an::OutputIntent::create(intent->toRequest());
    ASSERT_TRUE(rebuilt.hasValue()) << rebuilt.status().message();
    EXPECT_EQ(*rebuilt, *intent);
}

TEST(OutputIntentTest, NoScalingCarriesNeitherNumber)
{
    an::Result<an::OutputIntent> intent = an::OutputIntent::create(sixteenBitRequest());
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();

    EXPECT_EQ(intent->scaling(), an::OutputScaling::None);
    EXPECT_FALSE(intent->scale().has_value());
    EXPECT_FALSE(intent->zeroOffset().has_value());
}

TEST(OutputIntentTest, AnExplicitScalingNeedsBothOfItsNumbers)
{
    an::OutputIntentRequest onlyScale = sixteenBitRequest();
    onlyScale.scaling = an::OutputScaling::Explicit;
    onlyScale.scale = 2.0;
    an::Result<an::OutputIntent> refusedScale = an::OutputIntent::create(onlyScale);
    ASSERT_FALSE(refusedScale.hasValue());
    EXPECT_EQ(refusedScale.code(), an::ErrorCode::IncompatibleArguments);

    an::OutputIntentRequest onlyOffset = sixteenBitRequest();
    onlyOffset.scaling = an::OutputScaling::Explicit;
    onlyOffset.zeroOffset = 1.0;
    EXPECT_FALSE(an::OutputIntent::create(onlyOffset).hasValue());

    an::OutputIntentRequest neither = sixteenBitRequest();
    neither.scaling = an::OutputScaling::Explicit;
    EXPECT_FALSE(an::OutputIntent::create(neither).hasValue());
}

TEST(OutputIntentTest, AScalingThatCannotBeUndoneIsRefused)
{
    for (const double scale : {0.0, std::numeric_limits<double>::infinity()})
    {
        an::OutputIntentRequest request = sixteenBitRequest();
        request.scaling = an::OutputScaling::Explicit;
        request.scale = scale;
        request.zeroOffset = 0.0;

        an::Result<an::OutputIntent> refused = an::OutputIntent::create(request);
        ASSERT_FALSE(refused.hasValue()) << scale;
        EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument) << scale;
    }
}

TEST(OutputIntentTest, ANumberBesideAScalingThatDoesNotUseOneIsRefused)
{
    for (const an::OutputScaling scaling : {an::OutputScaling::None, an::OutputScaling::NormalizeFullRange})
    {
        an::OutputIntentRequest request = sixteenBitRequest();
        request.scaling = scaling;
        request.scale = 1.0;
        request.zeroOffset = 0.0;

        an::Result<an::OutputIntent> refused = an::OutputIntent::create(request);
        ASSERT_FALSE(refused.hasValue()) << an::outputScalingName(scaling);
        EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments) << an::outputScalingName(scaling);
    }
}

TEST(OutputIntentTest, AByteOrderIsRefusedForAOneByteDestination)
{
    an::OutputIntentRequest request{.sampleType = an::SampleType::UInt8};
    request.byteOrder = an::ByteOrder::Big;
    an::Result<an::OutputIntent> refused = an::OutputIntent::create(request);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);

    request.byteOrder = an::ByteOrder::Native;
    EXPECT_TRUE(an::OutputIntent::create(request).hasValue());
}

TEST(OutputIntentTest, ARoundingRuleIsRefusedForAFloatingDestination)
{
    for (const an::SampleType type : {an::SampleType::Float32, an::SampleType::Float64})
    {
        an::OutputIntentRequest request{.sampleType = type};
        request.rounding = an::OutputRounding::Floor;

        an::Result<an::OutputIntent> refused = an::OutputIntent::create(request);
        ASSERT_FALSE(refused.hasValue()) << an::sampleTypeName(type);
        EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments) << an::sampleTypeName(type);

        request.rounding = an::OutputRounding::Nearest;
        EXPECT_TRUE(an::OutputIntent::create(request).hasValue()) << an::sampleTypeName(type);
    }
}

TEST(OutputIntentTest, EveryRoundingRuleIsAvailableForAnIntegerDestination)
{
    for (const an::OutputRounding rounding : {an::OutputRounding::Nearest, an::OutputRounding::TowardZero,
                                              an::OutputRounding::Floor, an::OutputRounding::Ceiling})
    {
        an::OutputIntentRequest request = sixteenBitRequest();
        request.rounding = rounding;
        EXPECT_TRUE(an::OutputIntent::create(request).hasValue()) << an::outputRoundingName(rounding);
    }
}

TEST(OutputIntentTest, AValueOutsideAnEnumerationIsRefusedBeforeAnyCombinationRule)
{
    an::OutputIntentRequest unknownType{.sampleType = static_cast<an::SampleType>(200)};
    EXPECT_EQ(an::OutputIntent::create(unknownType).code(), an::ErrorCode::InvalidArgument);

    an::OutputIntentRequest unknownOrder = sixteenBitRequest();
    unknownOrder.byteOrder = static_cast<an::ByteOrder>(200);
    EXPECT_EQ(an::OutputIntent::create(unknownOrder).code(), an::ErrorCode::InvalidArgument);

    an::OutputIntentRequest unknownScaling = sixteenBitRequest();
    unknownScaling.scaling = static_cast<an::OutputScaling>(200);
    EXPECT_EQ(an::OutputIntent::create(unknownScaling).code(), an::ErrorCode::InvalidArgument);

    an::OutputIntentRequest unknownRounding = sixteenBitRequest();
    unknownRounding.rounding = static_cast<an::OutputRounding>(200);
    EXPECT_EQ(an::OutputIntent::create(unknownRounding).code(), an::ErrorCode::InvalidArgument);

    an::OutputIntentRequest unknownCompression = sixteenBitRequest();
    unknownCompression.compression = static_cast<an::OutputCompression>(200);
    EXPECT_EQ(an::OutputIntent::create(unknownCompression).code(), an::ErrorCode::InvalidArgument);
}

TEST(OutputIntentTest, MoreCardsThanOneImageWritesIsRefusedWithTheNumbers)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "COMMENT", "x", "");
    ASSERT_TRUE(card.hasValue());

    an::OutputIntentRequest request = sixteenBitRequest();
    request.cards.reserve(an::kMaxCodecCards + 1u);
    for (std::size_t index = 0u; index <= an::kMaxCodecCards; ++index)
    {
        request.cards.push_back(*card);
    }

    an::Result<an::OutputIntent> refused = an::OutputIntent::create(request);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    const an::StatusContextField* const limit = refused.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->count, an::kMaxCodecCards);
}

TEST(OutputIntentTest, TwoIntentsDifferWhenAnyDecisionDoes)
{
    an::Result<an::OutputIntent> left = an::OutputIntent::create(sixteenBitRequest());
    an::Result<an::OutputIntent> right = an::OutputIntent::create(sixteenBitRequest());
    ASSERT_TRUE(left.hasValue());
    ASSERT_TRUE(right.hasValue());
    EXPECT_EQ(*left, *right);

    an::OutputIntentRequest compressed = sixteenBitRequest();
    compressed.compression = an::OutputCompression::Gzip;
    an::Result<an::OutputIntent> different = an::OutputIntent::create(compressed);
    ASSERT_TRUE(different.hasValue());
    EXPECT_NE(*different, *left);
}

TEST(OutputIntentTest, EveryPolicyHasItsOwnNameAndAStrangerSaysSo)
{
    constexpr an::OutputScaling kScalings[] = {an::OutputScaling::None, an::OutputScaling::Explicit,
                                               an::OutputScaling::NormalizeFullRange};
    for (const an::OutputScaling left : kScalings)
    {
        EXPECT_TRUE(an::isOutputScaling(left));
        for (const an::OutputScaling right : kScalings)
        {
            EXPECT_EQ(std::strcmp(an::outputScalingName(left), an::outputScalingName(right)) == 0, left == right);
        }
    }

    constexpr an::OutputRounding kRoundings[] = {an::OutputRounding::Nearest, an::OutputRounding::TowardZero,
                                                 an::OutputRounding::Floor, an::OutputRounding::Ceiling};
    for (const an::OutputRounding left : kRoundings)
    {
        EXPECT_TRUE(an::isOutputRounding(left));
        for (const an::OutputRounding right : kRoundings)
        {
            EXPECT_EQ(std::strcmp(an::outputRoundingName(left), an::outputRoundingName(right)) == 0, left == right);
        }
    }

    constexpr an::OutputCompression kCompressions[] = {an::OutputCompression::None,    an::OutputCompression::Rice,
                                                       an::OutputCompression::Gzip,    an::OutputCompression::Plio,
                                                       an::OutputCompression::Deflate, an::OutputCompression::Lzw,
                                                       an::OutputCompression::Jpeg,    an::OutputCompression::Lz4,
                                                       an::OutputCompression::Lz4Hc,   an::OutputCompression::Zstd};
    for (const an::OutputCompression left : kCompressions)
    {
        EXPECT_TRUE(an::isOutputCompression(left));
        for (const an::OutputCompression right : kCompressions)
        {
            EXPECT_EQ(std::strcmp(an::outputCompressionName(left), an::outputCompressionName(right)) == 0,
                      left == right);
        }
    }

    constexpr an::OutputChecksum kChecksums[] = {an::OutputChecksum::None,          an::OutputChecksum::Sha1,
                                                 an::OutputChecksum::Sha256,        an::OutputChecksum::Sha512,
                                                 an::OutputChecksum::Sha3Digest256, an::OutputChecksum::Sha3Digest512};
    for (const an::OutputChecksum left : kChecksums)
    {
        EXPECT_TRUE(an::isOutputChecksum(left));
        for (const an::OutputChecksum right : kChecksums)
        {
            EXPECT_EQ(std::strcmp(an::outputChecksumName(left), an::outputChecksumName(right)) == 0, left == right);
        }
    }
    EXPECT_FALSE(an::isOutputCompression(static_cast<an::OutputCompression>(10)));
    EXPECT_FALSE(an::isOutputChecksum(static_cast<an::OutputChecksum>(6)));

    EXPECT_STREQ(an::outputChecksumName(static_cast<an::OutputChecksum>(200)), "UnknownOutputChecksum");
    EXPECT_STREQ(an::outputScalingName(static_cast<an::OutputScaling>(200)), "UnknownOutputScaling");
    EXPECT_STREQ(an::outputRoundingName(static_cast<an::OutputRounding>(200)), "UnknownOutputRounding");
    EXPECT_STREQ(an::outputCompressionName(static_cast<an::OutputCompression>(200)), "UnknownOutputCompression");
}

TEST(OutputIntentTest, ALevelBelongsToACompressionThatTakesOneAndIsCarriedUnchanged)
{
    an::OutputIntentRequest request = sixteenBitRequest();
    request.compression = an::OutputCompression::Deflate;
    request.compressionLevel = 9u;
    an::Result<an::OutputIntent> deflate = an::OutputIntent::create(request);
    ASSERT_TRUE(deflate.hasValue()) << deflate.status().message();
    ASSERT_TRUE(deflate->compressionLevel().has_value());
    EXPECT_EQ(*deflate->compressionLevel(), 9u);
    EXPECT_EQ(deflate->toRequest().compressionLevel, deflate->compressionLevel());

    request.compression = an::OutputCompression::Jpeg;
    request.compressionLevel = 100u;
    an::Result<an::OutputIntent> quality = an::OutputIntent::create(request);
    ASSERT_TRUE(quality.hasValue()) << quality.status().message();
    EXPECT_EQ(*quality->compressionLevel(), 100u);

    request.compressionLevel.reset();
    an::Result<an::OutputIntent> unstated = an::OutputIntent::create(request);
    ASSERT_TRUE(unstated.hasValue());
    EXPECT_FALSE(unstated->compressionLevel().has_value())
        << "an absent level asks for the destination codec's own default";
}

TEST(OutputIntentTest, ALevelOutsideItsCompressionsRangeOrBesideOneWithoutALevelIsRefused)
{
    an::OutputIntentRequest request = sixteenBitRequest();
    request.compression = an::OutputCompression::Deflate;
    request.compressionLevel = 10u;
    EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::InvalidArgument);

    request.compression = an::OutputCompression::Jpeg;
    request.compressionLevel = 0u;
    EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::InvalidArgument);
    request.compressionLevel = 101u;
    EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::InvalidArgument);

    request.compression = an::OutputCompression::Lzw;
    request.compressionLevel = 5u;
    EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::IncompatibleArguments)
        << "a number carried beside a compression that reads none is one something would eventually apply";

    request.compression = an::OutputCompression::None;
    request.compressionLevel = 1u;
    EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::IncompatibleArguments);

    request.compressionLevel.reset();
    EXPECT_TRUE(an::OutputIntent::create(request).hasValue());
}

TEST(OutputIntentTest, EachCodecTakesTheLevelsOfItsOwnRangeAndLz4TakesNone)
{
    // LZ4-HC from one to twelve and Zstandard from one to twenty-two, both ends included; plain LZ4 has no levels, so
    // a level beside it is a number nothing would apply.
    for (const auto& [compression, lowest, highest] :
         std::vector<std::tuple<an::OutputCompression, std::uint32_t, std::uint32_t>>{
             {an::OutputCompression::Lz4Hc, 1u, 12u}, {an::OutputCompression::Zstd, 1u, 22u}})
    {
        an::OutputIntentRequest request = sixteenBitRequest();
        request.compression = compression;
        for (const std::uint32_t level : {lowest, highest})
        {
            request.compressionLevel = level;
            const an::Result<an::OutputIntent> intent = an::OutputIntent::create(request);
            ASSERT_TRUE(intent.hasValue()) << an::outputCompressionName(compression) << " " << level;
            EXPECT_EQ(intent->compressionLevel(), level);
        }
        for (const std::uint32_t level : {lowest - 1u, highest + 1u})
        {
            request.compressionLevel = level;
            EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::InvalidArgument)
                << an::outputCompressionName(compression) << " " << level;
        }
    }
    an::OutputIntentRequest request = sixteenBitRequest();
    request.compression = an::OutputCompression::Lz4;
    EXPECT_TRUE(an::OutputIntent::create(request).hasValue());
    request.compressionLevel = 1u;
    EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(OutputIntentTest, TheFormOfACompressedPayloadIsCarriedUnchanged)
{
    // Byte shuffling, the size of the pieces compressed on their own and the checksum are carried as stated, read
    // back through the request, and make two intents differ; an unstated shuffle or block size is the destination
    // codec's default and reads back unstated.
    an::OutputIntentRequest request = sixteenBitRequest();
    request.compression = an::OutputCompression::Zstd;
    request.byteShuffle = false;
    request.compressionBlockBytes = std::uint64_t{1u} << 20u;
    request.checksum = an::OutputChecksum::Sha512;
    const an::Result<an::OutputIntent> stated = an::OutputIntent::create(request);
    ASSERT_TRUE(stated.hasValue()) << stated.status().message();
    EXPECT_EQ(stated->byteShuffle(), false);
    EXPECT_EQ(stated->compressionBlockBytes(), std::uint64_t{1u} << 20u);
    EXPECT_EQ(stated->checksum(), an::OutputChecksum::Sha512);
    const an::OutputIntentRequest back = stated->toRequest();
    EXPECT_EQ(back.byteShuffle, request.byteShuffle);
    EXPECT_EQ(back.compressionBlockBytes, request.compressionBlockBytes);
    EXPECT_EQ(back.checksum, request.checksum);
    EXPECT_EQ(*an::OutputIntent::create(back), *stated);

    for (const auto& change : std::vector<std::function<void(an::OutputIntentRequest&)>>{
             [](an::OutputIntentRequest& other) { other.byteShuffle = true; },
             [](an::OutputIntentRequest& other) { other.compressionBlockBytes = 4096u; },
             [](an::OutputIntentRequest& other) { other.checksum = an::OutputChecksum::Sha1; }})
    {
        an::OutputIntentRequest other = request;
        change(other);
        EXPECT_NE(*an::OutputIntent::create(other), *stated);
    }

    request.byteShuffle.reset();
    request.compressionBlockBytes.reset();
    const an::Result<an::OutputIntent> defaults = an::OutputIntent::create(request);
    ASSERT_TRUE(defaults.hasValue());
    EXPECT_FALSE(defaults->byteShuffle().has_value());
    EXPECT_FALSE(defaults->compressionBlockBytes().has_value());

    // A checksum describes the stored bytes, compressed or not.
    request.compression = an::OutputCompression::None;
    const an::Result<an::OutputIntent> plain = an::OutputIntent::create(request);
    ASSERT_TRUE(plain.hasValue());
    EXPECT_EQ(plain->checksum(), an::OutputChecksum::Sha512);
}

TEST(OutputIntentTest, ShufflingAndABlockSizeAreStepsOfACompressionAndARefusedOneNamesItself)
{
    an::OutputIntentRequest request = sixteenBitRequest();
    request.byteShuffle = true;
    an::Result<an::OutputIntent> shuffled = an::OutputIntent::create(request);
    EXPECT_EQ(shuffled.status().code(), an::ErrorCode::IncompatibleArguments)
        << "a shuffle stated beside no compression has nothing to shuffle for";
    ASSERT_NE(shuffled.status().findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(shuffled.status().findContext(an::ContextKey::Parameter)->text, "byteShuffle");
    request.byteShuffle = false;
    EXPECT_EQ(an::OutputIntent::create(request).status().code(), an::ErrorCode::IncompatibleArguments)
        << "either value is a statement about a compression";

    request.byteShuffle.reset();
    request.compressionBlockBytes = 1024u;
    an::Result<an::OutputIntent> blocked = an::OutputIntent::create(request);
    EXPECT_EQ(blocked.status().code(), an::ErrorCode::IncompatibleArguments);
    ASSERT_NE(blocked.status().findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(blocked.status().findContext(an::ContextKey::Parameter)->text, "compressionBlockBytes");

    request.compression = an::OutputCompression::Lz4;
    request.compressionBlockBytes = 0u;
    an::Result<an::OutputIntent> empty = an::OutputIntent::create(request);
    EXPECT_EQ(empty.status().code(), an::ErrorCode::InvalidArgument) << "a block of no bytes holds nothing";
    request.compressionBlockBytes = 1u;
    EXPECT_TRUE(an::OutputIntent::create(request).hasValue());

    request.checksum = static_cast<an::OutputChecksum>(6);
    an::Result<an::OutputIntent> stranger = an::OutputIntent::create(request);
    EXPECT_EQ(stranger.status().code(), an::ErrorCode::InvalidArgument);
    ASSERT_NE(stranger.status().findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(stranger.status().findContext(an::ContextKey::Parameter)->text, "checksum");
}

} // namespace

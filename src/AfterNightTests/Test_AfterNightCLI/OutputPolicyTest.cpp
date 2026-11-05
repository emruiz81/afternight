// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Which representation the command-line tool writes, and which compression it states, for every container the engine
// installs.
//
// The encode sets are read from the engine's own codecs rather than written here, so a codec that learns a
// representation changes what the tool writes and these cases say whether the rule still holds.

#include "OutputPolicy.h"

#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace
{

constexpr std::array<an::SampleType, 8u> kEveryType{
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

/// The encode set of one of the engine's codecs.
std::uint32_t encodeSetOf(const an::Result<std::shared_ptr<const an::ImageCodec>>& codec)
{
    EXPECT_TRUE(codec.hasValue());
    return codec.hasValue() ? (*codec)->capabilities().encodeSamples : 0u;
}

/// An encode set holding exactly these representations.
std::uint32_t setOf(std::initializer_list<an::SampleType> types)
{
    std::uint32_t bits = 0u;
    for (const an::SampleType type : types)
    {
        bits |= an::codecSampleBit(an::rawSampleFormatOf(type));
    }
    return bits;
}

} // namespace

TEST(OutputPolicyTest, ARepresentationTheContainerHoldsIsWrittenAsItIs)
{
    const std::uint32_t fits = encodeSetOf(an::createFitsCodec());
    const std::uint32_t tiff = encodeSetOf(an::createTiffCodec());
    for (const an::SampleType type : kEveryType)
    {
        EXPECT_EQ(an::cli::chooseOutputSampleType(type, std::nullopt, fits), type) << an::sampleTypeName(type);
        EXPECT_EQ(an::cli::chooseOutputSampleType(type, std::nullopt, tiff), type) << an::sampleTypeName(type);
    }
}

TEST(OutputPolicyTest, ARepresentationTheContainerCannotHoldBecomesTheNearestOneItCan)
{
    // PNG holds eight and sixteen unsigned bits, and every representation becomes one of them.
    const std::uint32_t png = encodeSetOf(an::createPngCodec());
    ASSERT_EQ(png, setOf({an::SampleType::UInt8, an::SampleType::UInt16}));
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::UInt8, std::nullopt, png), an::SampleType::UInt8);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int8, std::nullopt, png), an::SampleType::UInt8);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int16, std::nullopt, png), an::SampleType::UInt16);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::UInt32, std::nullopt, png), an::SampleType::UInt16);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int32, std::nullopt, png), an::SampleType::UInt16);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Float32, std::nullopt, png), an::SampleType::UInt16);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Float64, std::nullopt, png), an::SampleType::UInt16);

    // JPEG holds eight unsigned bits and nothing else.
    const std::uint32_t jpeg = encodeSetOf(an::createJpegCodec());
    for (const an::SampleType type : kEveryType)
    {
        EXPECT_EQ(an::cli::chooseOutputSampleType(type, std::nullopt, jpeg), an::SampleType::UInt8)
            << an::sampleTypeName(type);
    }

    // XISF holds no signed integers: each becomes the unsigned one of its width, which is the input's kind of
    // integer before floating point.
    const std::uint32_t xisf = encodeSetOf(an::createXisfCodec());
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int8, std::nullopt, xisf), an::SampleType::UInt8);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int16, std::nullopt, xisf), an::SampleType::UInt16);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int32, std::nullopt, xisf), an::SampleType::UInt32);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Float32, std::nullopt, xisf), an::SampleType::Float32);
}

TEST(OutputPolicyTest, TiesArePreferredByTheInputsOwnKindOfNumber)
{
    // Four bytes as an unsigned integer or as floating point: a floating input stays floating, an integer input
    // stays an integer.
    const std::uint32_t fourBytes = setOf({an::SampleType::UInt32, an::SampleType::Float32});
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Float64, std::nullopt, fourBytes),
              an::SampleType::Float32);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int32, std::nullopt, fourBytes), an::SampleType::UInt32);
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Int16, std::nullopt, fourBytes), an::SampleType::UInt32);

    // Signed before floating point once the input's own kind is not there.
    const std::uint32_t signedOrFloat = setOf({an::SampleType::Int32, an::SampleType::Float32});
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::UInt16, std::nullopt, signedOrFloat),
              an::SampleType::Int32);

    // Width decides before kind: the narrowest that holds the input wins whatever its kind.
    const std::uint32_t narrowSigned = setOf({an::SampleType::Int16, an::SampleType::Float32});
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::UInt8, std::nullopt, narrowSigned),
              an::SampleType::Int16);
}

TEST(OutputPolicyTest, ARequestedRepresentationIsWrittenOrRefusedAndNeverReplaced)
{
    const std::uint32_t png = encodeSetOf(an::createPngCodec());
    EXPECT_EQ(an::cli::chooseOutputSampleType(an::SampleType::Float32, an::SampleType::UInt8, png),
              an::SampleType::UInt8);
    EXPECT_FALSE(an::cli::chooseOutputSampleType(an::SampleType::UInt16, an::SampleType::Float32, png).has_value());
    EXPECT_FALSE(an::cli::chooseOutputSampleType(an::SampleType::UInt16, an::SampleType::UInt16, 0u).has_value());
    EXPECT_FALSE(an::cli::chooseOutputSampleType(an::SampleType::UInt16, std::nullopt, 0u).has_value());
}

TEST(OutputPolicyTest, EachContainerIsWrittenWithTheCompressionItAlwaysUsesOrNone)
{
    EXPECT_EQ(an::cli::outputCompression(an::CodecFormat::Png), an::OutputCompression::Deflate);
    EXPECT_EQ(an::cli::outputCompression(an::CodecFormat::Jpeg), an::OutputCompression::Jpeg);
    EXPECT_EQ(an::cli::outputCompression(an::CodecFormat::Fits), an::OutputCompression::None);
    EXPECT_EQ(an::cli::outputCompression(an::CodecFormat::Xisf), an::OutputCompression::None);
    EXPECT_EQ(an::cli::outputCompression(an::CodecFormat::Tiff), an::OutputCompression::None);
}

TEST(OutputPolicyTest, OnlyAContainerThatStatesAFilterArrayHoldsAMosaic)
{
    EXPECT_TRUE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Fits, an::CfaPattern::Rggb));
    EXPECT_TRUE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Xisf, an::CfaPattern::Rggb));
    EXPECT_TRUE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Tiff, an::CfaPattern::Rggb));
    EXPECT_FALSE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Png, an::CfaPattern::Rggb));
    EXPECT_FALSE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Jpeg, an::CfaPattern::Rggb));
    EXPECT_FALSE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Raw, an::CfaPattern::Rggb));
}

// FITS states a mosaic's filters in one two-by-two pattern card and its codec refuses a tabulated mosaic, but the tool
// took FITS for a container that holds every mosaic, so an X-Trans camera file written as FITS failed in the codec, in
// the codec's words, after the decode and the stretch. Only XISF and TIFF hold an X-Trans mosaic, and each of the four
// two-by-two arrangements is still held by FITS.
TEST(OutputPolicyTest, FitsHoldsATwoByTwoMosaicAndNotAnXTransOne)
{
    EXPECT_FALSE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Fits, an::CfaPattern::XTrans));
    EXPECT_TRUE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Xisf, an::CfaPattern::XTrans));
    EXPECT_TRUE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Tiff, an::CfaPattern::XTrans));
    EXPECT_FALSE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Png, an::CfaPattern::XTrans));
    EXPECT_FALSE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Jpeg, an::CfaPattern::XTrans));
    for (const an::CfaPattern pattern :
         {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Gbrg, an::CfaPattern::Grbg})
    {
        EXPECT_TRUE(an::cli::holdsFilterArrayMosaic(an::CodecFormat::Fits, pattern)) << an::cfaPatternName(pattern);
    }
}

TEST(OutputPolicyTest, WhatAContainerHoldsIsSpelledAsTheToolSpellsIt)
{
    EXPECT_EQ(an::cli::describeEncodableSampleTypes(setOf({an::SampleType::UInt8, an::SampleType::UInt16})),
              "uint8 and uint16");
    EXPECT_EQ(an::cli::describeEncodableSampleTypes(setOf({an::SampleType::UInt8})), "uint8");
    EXPECT_EQ(an::cli::describeEncodableSampleTypes(
                  setOf({an::SampleType::UInt8, an::SampleType::Int16, an::SampleType::Float64})),
              "uint8, int16 and float64");
    EXPECT_EQ(an::cli::describeEncodableSampleTypes(0u), "nothing");
    EXPECT_EQ(an::cli::encodableSampleTypes(setOf({an::SampleType::Float32, an::SampleType::UInt8})),
              (std::vector<an::SampleType>{an::SampleType::UInt8, an::SampleType::Float32}));
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/RawFrame.h.
//
// Three properties are the reason this type exists.
//
// **A frame outside the eight representations can be described and carried.** A 64-bit integer
// image is a real file format; here it is a frame with a complete description that simply has no
// resident sample type, which is what stops it becoming a processing image behind somebody's back.
//
// **Adoption is decided once, and the refusal says which of three things is wrong.** An unsupported
// representation needs a conversion, foreign byte order needs a swap, and memory the engine does not
// own needs a copy - and a caller acts differently on each, so one refusal for all three would be a
// refusal nobody can act on.
//
// **The description is refused for the same reasons a resident one is.** The combination rules are
// the descriptor's rules, applied at the boundary, so a decoder and an image cannot describe one
// picture in two different languages.

#include <AfterNightEngine/Image/RawFrame.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/DescriptorText.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

/// An engine context with the default budget.
///
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.limits = an::defaultEngineLimits();

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// A validated extent.
///
/// @param width the frame's width.
/// @param height the frame's height.
/// @param channelCount how many channels.
/// @return the extent.
[[nodiscard]] an::ImageExtent makeExtent(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channelCount, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return extent.value();
}

/// A monochrome boundary description with a stated representation and order.
///
/// @param extent the frame's dimensions.
/// @param format what one sample is.
/// @param order in which order its bytes sit.
/// @return the request, whose meaning fields are the ones an uncalibrated sensor frame has.
[[nodiscard]] an::RawFrameDescriptorRequest monoRequest(const an::ImageExtent& extent,
                                                        const an::RawSampleFormat& format,
                                                        an::ByteOrder order = an::ByteOrder::Native)
{
    return an::RawFrameDescriptorRequest{.extent = extent,
                                         .sampleFormat = format,
                                         .byteOrder = order,
                                         .channelSchema = an::ChannelSchema::Mono,
                                         .colorSpace = an::ColorSpace::Unspecified,
                                         .numericDomain = an::NumericDomain::RawSensorCode,
                                         .cfa = an::CfaDescriptor{},
                                         .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                         .pixelAspectRatio = 1.0};
}

/// A tightly packed single-plane layout for a boundary frame.
///
/// @param extent the frame's dimensions.
/// @param elementSizeBytes how wide one sample is.
/// @return the layout.
[[nodiscard]] an::ImageLayout packedLayout(const an::ImageExtent& extent, std::uint32_t elementSizeBytes)
{
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = extent,
                                                       .elementSizeBytes = elementSizeBytes,
                                                       .samplesPerPixel = extent.channelCount(),
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return layout.value();
}

/// Engine memory large enough for a layout, wrapped as a boundary buffer.
///
/// @param engine the context the memory comes from.
/// @param layout the geometry to size it for.
/// @return the buffer.
[[nodiscard]] an::SampleBuffer ownedBuffer(const an::EngineContext& engine, const an::ImageLayout& layout)
{
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    EXPECT_TRUE(buffer.hasValue());
    return buffer.hasValue() ? std::move(*buffer) : an::SampleBuffer{};
}

/// Something a lifetime token can point at.
struct DecoderBuffer
{
    /// The bytes the decoder produced.
    std::vector<std::byte> bytes;
};

TEST(RawFrameTest, ASixteenBitSensorFrameDescribesItselfCompletely)
{
    const an::ImageExtent extent = makeExtent(8u, 4u, 1u);
    const an::RawSampleFormat format = an::rawSampleFormatOf(an::SampleType::UInt16);
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, format, an::ByteOrder::Big));
    ASSERT_TRUE(described.has_value());

    EXPECT_EQ(described->extent(), extent);
    EXPECT_EQ(described->sampleFormat(), format);
    EXPECT_EQ(described->byteOrder(), an::ByteOrder::Big);
    EXPECT_EQ(described->channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(described->numericDomain(), an::NumericDomain::RawSensorCode);
    EXPECT_EQ(described->coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
    EXPECT_DOUBLE_EQ(described->pixelAspectRatio(), 1.0);
    EXPECT_FALSE(described->isMosaiced());

    const std::optional<an::SampleType> resident = described->residentSampleType();
    ASSERT_TRUE(resident.has_value());
    EXPECT_EQ(*resident, an::SampleType::UInt16);
}

TEST(RawFrameTest, ASixtyFourBitIntegerFrameIsDescribableAndHasNoResidentRepresentation)
{
    const an::ImageExtent extent = makeExtent(4u, 2u, 1u);
    const an::RawSampleFormat wide{.sizeBytes = 8u, .sampleClass = an::RawSampleClass::SignedInteger};
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, wide, an::ByteOrder::Big));
    ASSERT_TRUE(described.has_value());

    EXPECT_FALSE(described->residentSampleType().has_value());
    EXPECT_FALSE(described->toImageDescriptor().has_value());
}

TEST(RawFrameTest, AResidentDescriptionCrossesIntoTheProcessingVocabularyUnchanged)
{
    const an::ImageExtent extent = makeExtent(6u, 3u, 1u);
    const std::optional<an::RawFrameDescriptor> described = an::RawFrameDescriptor::create(
        monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16), an::ByteOrder::Big));
    ASSERT_TRUE(described.has_value());

    const std::optional<an::ImageDescriptor> resident = described->toImageDescriptor();
    ASSERT_TRUE(resident.has_value());
    EXPECT_EQ(resident->extent(), extent);
    EXPECT_EQ(resident->sampleType(), an::SampleType::UInt16);
    EXPECT_EQ(resident->channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(resident->numericDomain(), an::NumericDomain::RawSensorCode);
    EXPECT_EQ(resident->coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
    EXPECT_DOUBLE_EQ(resident->pixelAspectRatio(), 1.0);
}

TEST(RawFrameTest, AnImageDescriptionIsTheFrameDescriptionItWasBuiltFrom)
{
    // The way back out of the processing model. Every resident representation, a colour picture, a
    // mosaic with a phase, a bottom-up origin and a non-square pixel all cross in both directions
    // unchanged, and the frame's bytes are in the host's own order because an image's always are.
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(6u, 4u, 1u, an::defaultEngineLimits());
    const std::optional<an::ImageExtent> colour = an::ImageExtent::create(6u, 4u, 3u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    ASSERT_TRUE(colour.has_value());
    for (const an::SampleType sampleType :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        const std::optional<an::ImageDescriptor> mono =
            an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                   .sampleType = sampleType,
                                                                   .channelSchema = an::ChannelSchema::Mono,
                                                                   .colorSpace = an::ColorSpace::Unspecified,
                                                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                   .cfa = an::CfaDescriptor{},
                                                                   .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                                                   .pixelAspectRatio = 1.25});
        ASSERT_TRUE(mono.has_value()) << an::sampleTypeName(sampleType);
        const std::optional<an::RawFrameDescriptor> frame = an::RawFrameDescriptor::fromImageDescriptor(*mono);
        ASSERT_TRUE(frame.has_value()) << an::sampleTypeName(sampleType);
        EXPECT_EQ(frame->residentSampleType(), sampleType);
        EXPECT_TRUE(frame->isHostOrder());
        EXPECT_EQ(frame->toImageDescriptor(), mono);
    }

    const std::optional<an::CfaDescriptor> bayer = an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 0u);
    ASSERT_TRUE(bayer.has_value());
    const std::optional<an::ImageDescriptor> mosaic =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::UInt16,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::RawSensorCode,
                                                               .cfa = *bayer,
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    const std::optional<an::ImageDescriptor> rgb =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *colour,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Rgb,
                                                               .colorSpace = an::ColorSpace::LinearRgb,
                                                               .numericDomain = an::NumericDomain::PhysicalScaled,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    ASSERT_TRUE(mosaic.has_value());
    ASSERT_TRUE(rgb.has_value());
    for (const an::ImageDescriptor& described : {*mosaic, *rgb})
    {
        const std::optional<an::RawFrameDescriptor> frame = an::RawFrameDescriptor::fromImageDescriptor(described);
        ASSERT_TRUE(frame.has_value()) << an::formatImageDescriptor(described);
        EXPECT_EQ(frame->toImageDescriptor(), described) << an::formatImageDescriptor(described);
    }
}

TEST(RawFrameTest, TheDescriptionRoundTripsThroughItsRequest)
{
    const an::ImageExtent extent = makeExtent(8u, 8u, 1u);
    an::RawFrameDescriptorRequest request =
        monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16), an::ByteOrder::Big);
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 1u, 0u);
    ASSERT_TRUE(cfa.has_value());
    request.cfa = *cfa;

    const std::optional<an::RawFrameDescriptor> described = an::RawFrameDescriptor::create(request);
    ASSERT_TRUE(described.has_value());
    EXPECT_TRUE(described->isMosaiced());

    const std::optional<an::RawFrameDescriptor> rebuilt = an::RawFrameDescriptor::create(described->toRequest());
    ASSERT_TRUE(rebuilt.has_value());
    EXPECT_EQ(*rebuilt, *described);
}

TEST(RawFrameTest, AByteOrderIsRefusedForASampleThatOccupiesOneByte)
{
    const an::ImageExtent extent = makeExtent(4u, 4u, 1u);
    const an::RawSampleFormat narrow = an::rawSampleFormatOf(an::SampleType::UInt8);

    EXPECT_EQ(an::RawFrameDescriptor::check(monoRequest(extent, narrow, an::ByteOrder::Big)),
              an::DescriptorError::ByteOrderNotMeaningful);
    EXPECT_EQ(an::RawFrameDescriptor::check(monoRequest(extent, narrow, an::ByteOrder::Little)),
              an::DescriptorError::ByteOrderNotMeaningful);
    EXPECT_EQ(an::RawFrameDescriptor::check(monoRequest(extent, narrow, an::ByteOrder::Native)),
              an::DescriptorError::None);
}

TEST(RawFrameTest, AnUnknownFormatOrOrderIsRefusedBeforeAnyRuleInterpretsIt)
{
    const an::ImageExtent extent = makeExtent(4u, 4u, 1u);

    EXPECT_EQ(an::RawFrameDescriptor::check(monoRequest(
                  extent, an::RawSampleFormat{.sizeBytes = 4u, .sampleClass = static_cast<an::RawSampleClass>(9)})),
              an::DescriptorError::UnknownRawSampleClass);
    EXPECT_EQ(an::RawFrameDescriptor::check(monoRequest(
                  extent, an::RawSampleFormat{.sizeBytes = 3u, .sampleClass = an::RawSampleClass::UnsignedInteger})),
              an::DescriptorError::RawSampleSizeNotSupported);
    EXPECT_EQ(an::RawFrameDescriptor::check(
                  monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16), static_cast<an::ByteOrder>(9))),
              an::DescriptorError::UnknownByteOrder);
}

TEST(RawFrameTest, TheCombinationRulesAreTheDescriptorsRulesAppliedAtTheBoundary)
{
    const an::ImageExtent threeChannels = makeExtent(4u, 4u, 3u);
    const an::RawSampleFormat sixteenBit = an::rawSampleFormatOf(an::SampleType::UInt16);

    an::RawFrameDescriptorRequest wrongCount = monoRequest(threeChannels, sixteenBit);
    EXPECT_EQ(an::RawFrameDescriptor::check(wrongCount), an::DescriptorError::ChannelCountMismatch);

    an::RawFrameDescriptorRequest wrongBasis = monoRequest(makeExtent(4u, 4u, 3u), sixteenBit);
    wrongBasis.channelSchema = an::ChannelSchema::Lab;
    wrongBasis.colorSpace = an::ColorSpace::DisplayRgb;
    EXPECT_EQ(an::RawFrameDescriptor::check(wrongBasis), an::DescriptorError::ColorSpaceMismatch);

    an::RawFrameDescriptorRequest mosaicOverColour = monoRequest(makeExtent(4u, 4u, 3u), sixteenBit);
    mosaicOverColour.channelSchema = an::ChannelSchema::Rgb;
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(cfa.has_value());
    mosaicOverColour.cfa = *cfa;
    EXPECT_EQ(an::RawFrameDescriptor::check(mosaicOverColour), an::DescriptorError::CfaRequiresSingleChannel);

    an::RawFrameDescriptorRequest badAspect = monoRequest(makeExtent(4u, 4u, 1u), sixteenBit);
    badAspect.pixelAspectRatio = 0.0;
    EXPECT_EQ(an::RawFrameDescriptor::check(badAspect), an::DescriptorError::PixelAspectNotPositive);
}

TEST(RawFrameTest, APhysicalQuantityNeedsAFloatingFormatWhateverItsWidth)
{
    const an::ImageExtent extent = makeExtent(4u, 4u, 1u);

    an::RawFrameDescriptorRequest integerPhysical =
        monoRequest(extent, an::RawSampleFormat{.sizeBytes = 8u, .sampleClass = an::RawSampleClass::SignedInteger});
    integerPhysical.numericDomain = an::NumericDomain::PhysicalScaled;
    EXPECT_EQ(an::RawFrameDescriptor::check(integerPhysical), an::DescriptorError::NumericDomainNotRepresentable);

    an::RawFrameDescriptorRequest floatingPhysical =
        monoRequest(extent, an::RawSampleFormat{.sizeBytes = 16u, .sampleClass = an::RawSampleClass::FloatingPoint});
    floatingPhysical.numericDomain = an::NumericDomain::PhysicalScaled;
    EXPECT_EQ(an::RawFrameDescriptor::check(floatingPhysical), an::DescriptorError::None);
}

TEST(RawFrameTest, HostOrderIsWhatDecidesWhetherTheBytesNeedSwapping)
{
    const an::ImageExtent extent = makeExtent(4u, 4u, 1u);
    const an::RawSampleFormat sixteenBit = an::rawSampleFormatOf(an::SampleType::UInt16);

    const std::optional<an::RawFrameDescriptor> native =
        an::RawFrameDescriptor::create(monoRequest(extent, sixteenBit, an::ByteOrder::Native));
    ASSERT_TRUE(native.has_value());
    EXPECT_TRUE(native->isHostOrder());

    const std::optional<an::RawFrameDescriptor> hostStated =
        an::RawFrameDescriptor::create(monoRequest(extent, sixteenBit, an::nativeByteOrder()));
    ASSERT_TRUE(hostStated.has_value());
    EXPECT_TRUE(hostStated->isHostOrder());

    const an::ByteOrder foreign =
        an::nativeByteOrder() == an::ByteOrder::Little ? an::ByteOrder::Big : an::ByteOrder::Little;
    const std::optional<an::RawFrameDescriptor> swapped =
        an::RawFrameDescriptor::create(monoRequest(extent, sixteenBit, foreign));
    ASSERT_TRUE(swapped.has_value());
    EXPECT_FALSE(swapped->isHostOrder());
}

TEST(RawFrameTest, AFramePairsItsDescriptionItsGeometryAndItsBytes)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageExtent extent = makeExtent(8u, 4u, 1u);
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16)));
    ASSERT_TRUE(described.has_value());
    const an::ImageLayout layout = packedLayout(extent, 2u);

    an::Result<an::RawFrame> frame = an::RawFrame::create(*described, layout, ownedBuffer(engine, layout));
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    EXPECT_TRUE(frame->valid());
    EXPECT_EQ(frame->descriptor(), described);
    EXPECT_EQ(frame->layout(), layout);
    EXPECT_EQ(frame->bytes().size(), layout.totalBytes());
    EXPECT_TRUE(frame->buffer().adoptable());
}

TEST(RawFrameTest, ARowStopsBeforeThePaddingTheStridesDescribe)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageExtent extent = makeExtent(5u, 3u, 1u);
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16)));
    ASSERT_TRUE(described.has_value());

    // A 64-byte row alignment over a 10-byte row leaves 54 bytes per row a row span must not reach.
    const std::optional<an::ImageLayout> padded =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = extent,
                                                       .elementSizeBytes = 2u,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 64u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(padded.has_value());
    EXPECT_EQ(padded->rowStrideBytes(), 64u);

    an::Result<an::RawFrame> frame = an::RawFrame::create(*described, *padded, ownedBuffer(engine, *padded));
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    const std::optional<std::span<const std::byte>> row = frame->row(0u, 1u);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->size(), 10u);
    EXPECT_EQ(row->data(), frame->bytes().data() + 64u);

    const std::optional<std::span<std::byte>> writable = frame->mutableRow(0u, 2u);
    ASSERT_TRUE(writable.has_value());
    EXPECT_EQ(writable->size(), 10u);

    EXPECT_FALSE(frame->row(1u, 0u).has_value());
    EXPECT_FALSE(frame->row(0u, 3u).has_value());
}

TEST(RawFrameTest, ALayoutThatDescribesAnotherFrameIsRefusedAndTheBytesAreLeftWithTheCaller)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageExtent extent = makeExtent(8u, 4u, 1u);
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16)));
    ASSERT_TRUE(described.has_value());

    const an::ImageLayout wrongWidth = packedLayout(extent, 4u);
    an::SampleBuffer buffer = ownedBuffer(engine, wrongWidth);
    an::Result<an::RawFrame> refused = an::RawFrame::create(*described, wrongWidth, std::move(buffer));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);

    const an::ImageLayout wrongExtent = packedLayout(makeExtent(4u, 4u, 1u), 2u);
    an::SampleBuffer second = ownedBuffer(engine, wrongExtent);
    an::Result<an::RawFrame> mismatched = an::RawFrame::create(*described, wrongExtent, std::move(second));
    ASSERT_FALSE(mismatched.hasValue());
    EXPECT_EQ(mismatched.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(RawFrameTest, ABufferSmallerThanTheGeometryIsRefusedWithBothNumbers)
{
    const an::ImageExtent extent = makeExtent(8u, 4u, 1u);
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16)));
    ASSERT_TRUE(described.has_value());
    const an::ImageLayout layout = packedLayout(extent, 2u);

    std::array<std::byte, 16> tooSmall{};
    an::Result<an::SampleBuffer> buffer = an::SampleBuffer::borrow(std::span<std::byte>{tooSmall});
    ASSERT_TRUE(buffer.hasValue());

    an::Result<an::RawFrame> refused = an::RawFrame::create(*described, layout, std::move(*buffer));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::OutOfRange);

    const an::StatusContextField* const requested = refused.status().findContext(an::ContextKey::Requested);
    ASSERT_NE(requested, nullptr);
    EXPECT_EQ(requested->count, layout.totalBytes());

    const an::StatusContextField* const available = refused.status().findContext(an::ContextKey::Available);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(available->count, tooSmall.size());
}

TEST(RawFrameTest, AFrameNeedsBytes)
{
    const an::ImageExtent extent = makeExtent(4u, 4u, 1u);
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16)));
    ASSERT_TRUE(described.has_value());

    an::Result<an::RawFrame> refused = an::RawFrame::create(*described, packedLayout(extent, 2u), an::SampleBuffer{});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::NotInitialized);
}

TEST(RawFrameTest, EngineMemoryInHostOrderWithAResidentRepresentationIsAdoptable)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageExtent extent = makeExtent(8u, 4u, 1u);
    const std::optional<an::RawFrameDescriptor> described = an::RawFrameDescriptor::create(
        monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16), an::nativeByteOrder()));
    ASSERT_TRUE(described.has_value());
    const an::ImageLayout layout = packedLayout(extent, 2u);

    an::Result<an::RawFrame> frame = an::RawFrame::create(*described, layout, ownedBuffer(engine, layout));
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();
    EXPECT_TRUE(frame->checkAdoptable().ok());

    const std::byte* const address = frame->bytes().data();
    an::Result<an::AdoptedFrame> adopted = frame->releaseAdoptable();
    ASSERT_TRUE(adopted.hasValue()) << adopted.status().message();

    EXPECT_EQ(adopted->descriptor.sampleType(), an::SampleType::UInt16);
    EXPECT_EQ(adopted->layout, layout);
    EXPECT_TRUE(adopted->storage.valid());
    EXPECT_EQ(static_cast<const std::byte*>(adopted->storage.data()), address);
    EXPECT_FALSE(frame->valid());
}

TEST(RawFrameTest, AdoptionIsRefusedSeparatelyForEachOfItsThreeReasons)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageExtent extent = makeExtent(4u, 2u, 1u);

    // A representation outside the eight needs a conversion.
    const an::RawSampleFormat wide{.sizeBytes = 8u, .sampleClass = an::RawSampleClass::SignedInteger};
    const std::optional<an::RawFrameDescriptor> foreignFormat =
        an::RawFrameDescriptor::create(monoRequest(extent, wide, an::nativeByteOrder()));
    ASSERT_TRUE(foreignFormat.has_value());
    const an::ImageLayout wideLayout = packedLayout(extent, 8u);
    an::Result<an::RawFrame> unsupported =
        an::RawFrame::create(*foreignFormat, wideLayout, ownedBuffer(engine, wideLayout));
    ASSERT_TRUE(unsupported.hasValue()) << unsupported.status().message();
    EXPECT_EQ(unsupported->checkAdoptable().code(), an::ErrorCode::Unsupported);

    // Foreign byte order needs a swap.
    const an::ByteOrder foreign =
        an::nativeByteOrder() == an::ByteOrder::Little ? an::ByteOrder::Big : an::ByteOrder::Little;
    const std::optional<an::RawFrameDescriptor> swapped =
        an::RawFrameDescriptor::create(monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16), foreign));
    ASSERT_TRUE(swapped.has_value());
    const an::ImageLayout narrowLayout = packedLayout(extent, 2u);
    an::Result<an::RawFrame> ordered = an::RawFrame::create(*swapped, narrowLayout, ownedBuffer(engine, narrowLayout));
    ASSERT_TRUE(ordered.hasValue()) << ordered.status().message();
    EXPECT_EQ(ordered->checkAdoptable().code(), an::ErrorCode::IncompatibleArguments);

    // Memory the engine does not own needs a copy.
    const std::optional<an::RawFrameDescriptor> hostOrder = an::RawFrameDescriptor::create(
        monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16), an::nativeByteOrder()));
    ASSERT_TRUE(hostOrder.has_value());
    auto decoded = std::make_shared<DecoderBuffer>();
    decoded->bytes.resize(static_cast<std::size_t>(narrowLayout.totalBytes()));
    an::Result<an::SampleBuffer> external = an::SampleBuffer::external(
        an::SampleBufferOwnership::DecoderOwned, decoded->bytes.data(), decoded->bytes.size(), decoded, true);
    ASSERT_TRUE(external.hasValue());
    an::Result<an::RawFrame> borrowed = an::RawFrame::create(*hostOrder, narrowLayout, std::move(*external));
    ASSERT_TRUE(borrowed.hasValue()) << borrowed.status().message();
    EXPECT_EQ(borrowed->checkAdoptable().code(), an::ErrorCode::InvalidState);

    // None of the three consumed anything.
    EXPECT_TRUE(unsupported->valid());
    EXPECT_TRUE(ordered->valid());
    EXPECT_TRUE(borrowed->valid());
    EXPECT_FALSE(unsupported->releaseAdoptable().hasValue());
    EXPECT_FALSE(ordered->releaseAdoptable().hasValue());
    EXPECT_FALSE(borrowed->releaseAdoptable().hasValue());
    EXPECT_TRUE(unsupported->valid());
    EXPECT_TRUE(ordered->valid());
    EXPECT_TRUE(borrowed->valid());
}

TEST(RawFrameTest, AFrameThatHoldsNothingAnswersEveryQuestionWithoutBeingAsked)
{
    an::RawFrame frame;
    EXPECT_FALSE(frame.valid());
    EXPECT_FALSE(frame.descriptor().has_value());
    EXPECT_FALSE(frame.layout().has_value());
    EXPECT_TRUE(frame.bytes().empty());
    EXPECT_FALSE(frame.mutableBytes().has_value());
    EXPECT_FALSE(frame.row(0u, 0u).has_value());
    EXPECT_FALSE(frame.mutableRow(0u, 0u).has_value());
    EXPECT_EQ(frame.checkAdoptable().code(), an::ErrorCode::NotInitialized);

    frame.release();
    EXPECT_FALSE(frame.valid());
}

TEST(RawFrameTest, AReadOnlyFrameReadsAndRefusesEveryWritePath)
{
    const an::ImageExtent extent = makeExtent(4u, 2u, 1u);
    const std::optional<an::RawFrameDescriptor> described =
        an::RawFrameDescriptor::create(monoRequest(extent, an::rawSampleFormatOf(an::SampleType::UInt16)));
    ASSERT_TRUE(described.has_value());
    const an::ImageLayout layout = packedLayout(extent, 2u);

    const std::array<std::byte, 16> source{};
    an::Result<an::SampleBuffer> buffer = an::SampleBuffer::borrowConst(std::span<const std::byte>{source});
    ASSERT_TRUE(buffer.hasValue());
    an::Result<an::RawFrame> frame = an::RawFrame::create(*described, layout, std::move(*buffer));
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    EXPECT_EQ(frame->bytes().size(), source.size());
    EXPECT_TRUE(frame->row(0u, 0u).has_value());
    EXPECT_FALSE(frame->mutableBytes().has_value());
    EXPECT_FALSE(frame->mutableRow(0u, 0u).has_value());
}

} // namespace

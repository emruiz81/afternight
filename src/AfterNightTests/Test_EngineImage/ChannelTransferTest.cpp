// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ChannelTransfer.h.
//
// Taking a channel out of an image and putting channels back into one compute nothing, so what there
// is to get wrong is entirely in the addressing: which plane a channel is in, how far apart its
// samples are, and which of the two operations is allowed to touch which bytes. The cases below walk
// that addressing over every arrangement a layout can describe - one plane per channel, one plane
// holding every channel, and a mixture of the two - because an implementation that assumed a channel
// was a plane would pass every planar case and silently read the wrong samples of an interleaved
// image.
//
// One property here is not about a sample at all. A composition writes whatever arrangement its
// destination layout describes, and the arrangement of the result is read back from that layout
// rather than claimed by the operation. The failure that guards against is a buffer whose samples
// are in one arrangement while everything downstream describes it as the other, which produces no
// error, no wrong return value and no log line - only a picture whose channels are wrong somewhere
// much later. So the cases that compose assert both what the bytes are and what the layout is called.

#include <AfterNightEngine/Image/ChannelTransfer.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

/// How wide every image in this file is.
constexpr std::uint32_t kWidth = 5u;

/// How tall every image in this file is. Odd on both axes, so a vector body cannot cover a whole
/// row and every case exercises a tail.
constexpr std::uint32_t kHeight = 3u;

/// An engine context, which the case has already failed if it cannot build.
///
/// @return the context.
[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// An operation on a stated backend.
///
/// @param engine the context to start it on.
/// @param backend which implementation to run, or the engine's own choice.
/// @param stopToken a stop token, for the cancellation cases.
/// @return the operation.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::stop_token stopToken = std::stop_token{})
{
    an::ExecutionOptions options;
    options.backend = backend;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// An extent, which the case has already failed if it cannot build.
///
/// @param channelCount how many channels.
/// @return the extent, at this file's width and height.
[[nodiscard]] an::ImageExtent extentOf(std::uint32_t channelCount)
{
    const std::optional<an::ImageExtent> made =
        an::ImageExtent::create(kWidth, kHeight, channelCount, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A `Float32` layout of a stated arrangement and row padding.
///
/// @param channelCount how many channels the image has.
/// @param samplesPerPixel how many of a pixel's samples share a plane. The plane count follows.
/// @param rowPaddingSamples how many samples of padding follow each row.
/// @return the layout.
[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t channelCount, std::uint32_t samplesPerPixel,
                                       std::uint32_t rowPaddingSamples = 0u)
{
    const an::ImageLayoutRequest request{.extent = extentOf(channelCount),
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = channelCount / samplesPerPixel,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride =
        (static_cast<std::uint64_t>(kWidth) * samplesPerPixel + rowPaddingSamples) * sizeof(float);
    const std::optional<an::ImageLayout> made =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * kHeight, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A buffer sized for a layout, every slot set to one value.
///
/// @param layout where the samples sit.
/// @param value what to put in every slot, padding included.
/// @return the buffer.
[[nodiscard]] std::vector<float> bufferOf(const an::ImageLayout& layout, float value)
{
    return std::vector<float>(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), value);
}

/// The value a sample of a test image is given.
///
/// Distinct per channel and per pixel, so a case that reads the wrong channel or the wrong pixel
/// reports a number that says which.
///
/// @param channel which channel.
/// @param x the column.
/// @param y the row.
/// @return the value.
[[nodiscard]] float sampleValue(std::uint32_t channel, std::uint32_t x, std::uint32_t y)
{
    return static_cast<float>(channel * 1000u + y * 10u + x);
}

/// One sample of a described buffer.
///
/// @param samples the buffer.
/// @param layout where the samples sit.
/// @param channel which channel, counted across the whole image.
/// @param x the column.
/// @param y the row.
/// @return the sample, or a quiet sentinel when the address is out of range.
[[nodiscard]] float sampleAt(const std::vector<float>& samples, const an::ImageLayout& layout, std::uint32_t channel,
                             std::uint32_t x, std::uint32_t y)
{
    const std::uint32_t plane = channel / layout.samplesPerPixel();
    const std::uint32_t within = channel % layout.samplesPerPixel();
    const std::optional<std::uint64_t> offset = layout.sampleByteOffset(plane, y, x, within);
    EXPECT_TRUE(offset.has_value());
    if (!offset.has_value())
    {
        return -1.0f;
    }
    return samples[static_cast<std::size_t>(*offset / sizeof(float))];
}

/// A buffer holding a picture whose every sample says which channel and pixel it is.
///
/// @param layout where the samples sit.
/// @return the buffer, with the padding left at a value no sample takes.
[[nodiscard]] std::vector<float> pictureOf(const an::ImageLayout& layout)
{
    std::vector<float> samples = bufferOf(layout, -1.0f);
    for (std::uint32_t channel = 0u; channel < layout.extent().channelCount(); ++channel)
    {
        const std::uint32_t plane = channel / layout.samplesPerPixel();
        const std::uint32_t within = channel % layout.samplesPerPixel();
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::optional<std::uint64_t> offset = layout.sampleByteOffset(plane, y, x, within);
                EXPECT_TRUE(offset.has_value());
                samples[static_cast<std::size_t>(*offset / sizeof(float))] = sampleValue(channel, x, y);
            }
        }
    }
    return samples;
}

/// One channel-source descriptor over a whole buffer.
///
/// @param samples the buffer, which outlives the call that receives the descriptor.
/// @param layout where its samples sit, which outlives it too.
/// @return the descriptor.
[[nodiscard]] an::ChannelSource sourceOf(const std::vector<float>& samples, const an::ImageLayout& layout)
{
    return an::ChannelSource{.samples = samples.data(), .layout = &layout};
}

} // namespace

TEST(ChannelExtractionTest, EveryChannelOfAPlanarImageIsReachedByItsIndex)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout sourceLayout = layoutOf(3u, 1u, 2u);
    const an::ImageLayout monoLayout = layoutOf(1u, 1u, 1u);
    const std::vector<float> source = pictureOf(sourceLayout);

    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::vector<float> destination = bufferOf(monoLayout, -7.0f);
        ASSERT_TRUE(an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout, channel,
                                            destination.data(), monoLayout)
                        .ok());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_EQ(sampleAt(destination, monoLayout, 0u, x, y), sampleValue(channel, x, y));
            }
            // The one sample of padding after each row of the destination is not the picture, and an
            // extraction that wrote by buffer rather than by row would have covered it. The last
            // row's padding is past what the layout describes and so is not in the buffer at all.
            const std::size_t padding = static_cast<std::size_t>(y) * (kWidth + 1u) + kWidth;
            if (padding < destination.size())
            {
                EXPECT_EQ(destination[padding], -7.0f) << "row " << y;
            }
        }
    }
}

TEST(ChannelExtractionTest, AChannelOfAnInterleavedImageIsGatheredByItsStride)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout sourceLayout = layoutOf(3u, 3u, 1u);
    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const std::vector<float> source = pictureOf(sourceLayout);

    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::vector<float> destination = bufferOf(monoLayout, -7.0f);
        ASSERT_TRUE(an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout, channel,
                                            destination.data(), monoLayout)
                        .ok());
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_EQ(sampleAt(destination, monoLayout, 0u, x, y), sampleValue(channel, x, y));
            }
        }
    }
}

TEST(ChannelExtractionTest, AChannelOfASemiPlanarImageIsFoundInTheRightPlane)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    // Four channels in two planes of two: the arrangement a luminance plane beside an interleaved
    // chroma plane has, and the one an implementation that treated a channel as a plane gets wrong
    // in both directions at once.
    const an::ImageLayout sourceLayout = layoutOf(4u, 2u);
    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    ASSERT_EQ(an::storageArrangementOf(sourceLayout), an::StorageArrangement::SemiPlanar);
    const std::vector<float> source = pictureOf(sourceLayout);

    for (std::uint32_t channel = 0u; channel < 4u; ++channel)
    {
        std::vector<float> destination = bufferOf(monoLayout, -7.0f);
        ASSERT_TRUE(an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout, channel,
                                            destination.data(), monoLayout)
                        .ok());
        EXPECT_EQ(sampleAt(destination, monoLayout, 0u, 4u, 2u), sampleValue(channel, 4u, 2u));
    }
}

TEST(ChannelExtractionTest, AChannelTheSourceDoesNotHaveIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout sourceLayout = layoutOf(3u, 1u);
    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const std::vector<float> source = pictureOf(sourceLayout);
    std::vector<float> destination = bufferOf(monoLayout, -7.0f);

    const an::Status refused = an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout,
                                                       3u, destination.data(), monoLayout);
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(destination.front(), -7.0f);
}

TEST(ChannelExtractionTest, ADestinationThatIsNotAOneChannelPictureOfTheSameSizeIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout sourceLayout = layoutOf(3u, 1u);
    const std::vector<float> source = pictureOf(sourceLayout);

    const an::ImageLayout twoChannels = layoutOf(2u, 1u);
    std::vector<float> wide = bufferOf(twoChannels, -7.0f);
    EXPECT_EQ(an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout, 0u, wide.data(),
                                      twoChannels)
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    const std::optional<an::ImageExtent> shorter =
        an::ImageExtent::create(kWidth, kHeight - 1u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(shorter.has_value());
    const std::optional<an::ImageLayout> shortLayout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *shorter,
                                                       .elementSizeBytes = 4u,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                an::defaultEngineLimits());
    ASSERT_TRUE(shortLayout.has_value());
    std::vector<float> shortBuffer = bufferOf(*shortLayout, -7.0f);
    EXPECT_EQ(an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout, 0u,
                                      shortBuffer.data(), *shortLayout)
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ChannelExtractionTest, AnExtractionIntoItsOwnSourceIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout sourceLayout = layoutOf(3u, 1u);
    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    std::vector<float> samples = pictureOf(sourceLayout);
    const std::vector<float> before = samples;

    const an::Status refused = an::extractImageChannel(execution, an::SampleType::Float32, samples.data(), sourceLayout,
                                                       1u, samples.data(), monoLayout);
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(samples, before);
}

TEST(ChannelExtractionTest, AMisdescribedLayoutOrNullBufferIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout sourceLayout = layoutOf(3u, 1u);
    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const std::vector<float> source = pictureOf(sourceLayout);
    std::vector<float> destination = bufferOf(monoLayout, -7.0f);

    EXPECT_EQ(an::extractImageChannel(execution, an::SampleType::Float32, nullptr, sourceLayout, 0u, destination.data(),
                                      monoLayout)
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout, 0u, nullptr,
                                      monoLayout)
                  .code(),
              an::ErrorCode::InvalidArgument);
    // Sixteen-bit samples described by a four-byte layout: the pair that would read four times the
    // memory it was handed if nothing checked.
    EXPECT_EQ(an::extractImageChannel(execution, an::SampleType::UInt16, source.data(), sourceLayout, 0u,
                                      destination.data(), monoLayout)
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(ChannelCompositionTest, OneImagePerChannelIsJoinedInChannelOrder)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u, 1u);
    const an::ImageLayout destinationLayout = layoutOf(3u, 3u, 2u);

    std::vector<float> red = bufferOf(monoLayout, 0.0f);
    std::vector<float> green = bufferOf(monoLayout, 0.0f);
    std::vector<float> blue = bufferOf(monoLayout, 0.0f);
    std::array<std::vector<float>*, 3u> channels{&red, &green, &blue};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::optional<std::uint64_t> offset = monoLayout.sampleByteOffset(0u, y, x, 0u);
                ASSERT_TRUE(offset.has_value());
                (*channels[channel])[static_cast<std::size_t>(*offset / sizeof(float))] = sampleValue(channel, x, y);
            }
        }
    }

    std::vector<float> destination = bufferOf(destinationLayout, -7.0f);
    const std::array<an::ChannelSource, 3u> sources{sourceOf(red, monoLayout), sourceOf(green, monoLayout),
                                                    sourceOf(blue, monoLayout)};
    ASSERT_TRUE(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, destination.data(), destinationLayout)
            .ok());

    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_EQ(sampleAt(destination, destinationLayout, channel, x, y), sampleValue(channel, x, y));
            }
        }
    }
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const std::size_t rowStart = static_cast<std::size_t>(y) * (kWidth * 3u + 2u);
        for (std::size_t offset = kWidth * 3u; offset < kWidth * 3u + 2u; ++offset)
        {
            // As above: the buffer stops at the last sample, so the final row has no padding in it.
            if (rowStart + offset < destination.size())
            {
                EXPECT_EQ(destination[rowStart + offset], -7.0f) << "row " << y << " padding " << offset;
            }
        }
    }
}

TEST(ChannelCompositionTest, WhatWasBuiltIsWhatTheDestinationLayoutIsCalled)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout planarLayout = layoutOf(3u, 1u);
    const an::ImageLayout interleavedLayout = layoutOf(3u, 3u);
    ASSERT_EQ(an::storageArrangementOf(planarLayout), an::StorageArrangement::Planar);
    ASSERT_EQ(an::storageArrangementOf(interleavedLayout), an::StorageArrangement::Interleaved);

    const std::vector<float> mono = pictureOf(monoLayout);
    const std::array<an::ChannelSource, 3u> sources{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout),
                                                    sourceOf(mono, monoLayout)};

    std::vector<float> planar = bufferOf(planarLayout, -7.0f);
    ASSERT_TRUE(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, planar.data(), planarLayout).ok());
    std::vector<float> interleaved = bufferOf(interleavedLayout, -7.0f);
    ASSERT_TRUE(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, interleaved.data(), interleavedLayout)
            .ok());

    // The same three channels, both ways round. Neighbouring samples of a planar result are the same
    // channel of adjacent pixels and of an interleaved result are adjacent channels of one pixel, so
    // reading the two buffers as bytes distinguishes them without asking either what it is.
    EXPECT_EQ(planar[0], sampleValue(0u, 0u, 0u));
    EXPECT_EQ(planar[1], sampleValue(0u, 1u, 0u));
    EXPECT_EQ(interleaved[0], sampleValue(0u, 0u, 0u));
    EXPECT_EQ(interleaved[1], sampleValue(0u, 0u, 0u));
    EXPECT_EQ(interleaved[3], sampleValue(0u, 1u, 0u));

    // And the two buffers are not each other, which is the whole point: a comment claiming one of
    // these arrangements over the other one's bytes is the defect this pair exists to make visible.
    EXPECT_NE(planar, interleaved);
}

TEST(ChannelCompositionTest, TheWholePixelAndPerChannelPathsBuildTheSameImage)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u, 3u);
    const an::ImageLayout planarLayout = layoutOf(3u, 1u);
    const an::ImageLayout interleavedLayout = layoutOf(3u, 3u);

    std::vector<std::vector<float>> channels;
    channels.reserve(3u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        std::vector<float> samples = bufferOf(monoLayout, -1.0f);
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                const std::optional<std::uint64_t> offset = monoLayout.sampleByteOffset(0u, y, x, 0u);
                ASSERT_TRUE(offset.has_value());
                samples[static_cast<std::size_t>(*offset / sizeof(float))] = sampleValue(channel, x, y);
            }
        }
        channels.push_back(std::move(samples));
    }
    const std::array<an::ChannelSource, 3u> sources{
        sourceOf(channels[0], monoLayout), sourceOf(channels[1], monoLayout), sourceOf(channels[2], monoLayout)};

    // An interleaved destination of three channels takes the whole-pixel path; a planar one takes
    // the per-channel path. Repacking the planar result is an operation that shares no code with
    // either, so agreement between the three is evidence rather than a tautology.
    std::vector<float> fused = bufferOf(interleavedLayout, -7.0f);
    ASSERT_TRUE(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, fused.data(), interleavedLayout).ok());
    std::vector<float> planar = bufferOf(planarLayout, -7.0f);
    ASSERT_TRUE(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, planar.data(), planarLayout).ok());
    std::vector<float> repacked = bufferOf(interleavedLayout, -7.0f);
    ASSERT_TRUE(an::repackImageSamples(execution, an::SampleType::Float32, planar.data(), planarLayout, repacked.data(),
                                       interleavedLayout)
                    .ok());
    EXPECT_EQ(fused, repacked);
}

TEST(ChannelCompositionTest, OneImageMayBecomeEveryChannel)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout destinationLayout = layoutOf(3u, 3u);
    const std::vector<float> mono = pictureOf(monoLayout);
    std::vector<float> destination = bufferOf(destinationLayout, -7.0f);

    const std::array<an::ChannelSource, 3u> sources{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout),
                                                    sourceOf(mono, monoLayout)};
    ASSERT_TRUE(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, destination.data(), destinationLayout)
            .ok());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_EQ(sampleAt(destination, destinationLayout, channel, 3u, 1u), sampleValue(0u, 3u, 1u));
    }
}

TEST(ChannelCompositionTest, TheWrongNumberOfSourcesIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout destinationLayout = layoutOf(3u, 3u);
    const std::vector<float> mono = pictureOf(monoLayout);
    std::vector<float> destination = bufferOf(destinationLayout, -7.0f);

    const std::array<an::ChannelSource, 2u> tooFew{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout)};
    EXPECT_EQ(
        an::composeImageChannels(execution, an::SampleType::Float32, tooFew, destination.data(), destinationLayout)
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(destination.front(), -7.0f);

    const std::array<an::ChannelSource, 4u> tooMany{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout),
                                                    sourceOf(mono, monoLayout), sourceOf(mono, monoLayout)};
    EXPECT_EQ(
        an::composeImageChannels(execution, an::SampleType::Float32, tooMany, destination.data(), destinationLayout)
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(destination.front(), -7.0f);
}

TEST(ChannelCompositionTest, ASourceThatIsNotAOneChannelPictureOfTheSameSizeIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout pairLayout = layoutOf(2u, 1u);
    const an::ImageLayout destinationLayout = layoutOf(3u, 3u);
    const std::vector<float> mono = pictureOf(monoLayout);
    const std::vector<float> pair = pictureOf(pairLayout);
    std::vector<float> destination = bufferOf(destinationLayout, -7.0f);

    const std::array<an::ChannelSource, 3u> sources{sourceOf(mono, monoLayout), sourceOf(pair, pairLayout),
                                                    sourceOf(mono, monoLayout)};
    EXPECT_EQ(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, destination.data(), destinationLayout)
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(destination.front(), -7.0f);
}

TEST(ChannelCompositionTest, ASourceInsideTheDestinationIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout destinationLayout = layoutOf(3u, 3u);
    const std::vector<float> mono = pictureOf(monoLayout);
    std::vector<float> destination = bufferOf(destinationLayout, -7.0f);
    const std::vector<float> before = destination;

    const std::array<an::ChannelSource, 3u> sources{
        sourceOf(mono, monoLayout), an::ChannelSource{.samples = destination.data(), .layout = &monoLayout},
        sourceOf(mono, monoLayout)};
    EXPECT_EQ(
        an::composeImageChannels(execution, an::SampleType::Float32, sources, destination.data(), destinationLayout)
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(destination, before);
}

TEST(ChannelCompositionTest, ASourceMissingItsSamplesOrItsGeometryIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout destinationLayout = layoutOf(3u, 3u);
    const std::vector<float> mono = pictureOf(monoLayout);
    std::vector<float> destination = bufferOf(destinationLayout, -7.0f);

    const std::array<an::ChannelSource, 3u> noSamples{sourceOf(mono, monoLayout),
                                                      an::ChannelSource{.samples = nullptr, .layout = &monoLayout},
                                                      sourceOf(mono, monoLayout)};
    EXPECT_EQ(
        an::composeImageChannels(execution, an::SampleType::Float32, noSamples, destination.data(), destinationLayout)
            .code(),
        an::ErrorCode::InvalidArgument);

    const std::array<an::ChannelSource, 3u> noLayout{sourceOf(mono, monoLayout),
                                                     an::ChannelSource{.samples = mono.data(), .layout = nullptr},
                                                     sourceOf(mono, monoLayout)};
    EXPECT_EQ(
        an::composeImageChannels(execution, an::SampleType::Float32, noLayout, destination.data(), destinationLayout)
            .code(),
        an::ErrorCode::InvalidArgument);
    EXPECT_EQ(destination.front(), -7.0f);
}

TEST(ChannelTransferTest, AnOperationThatHoldsNothingMovesNoChannel)
{
    const an::ExecutionContext nothing;
    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout sourceLayout = layoutOf(3u, 1u);
    const std::vector<float> source = pictureOf(sourceLayout);
    const std::vector<float> mono = pictureOf(monoLayout);
    std::vector<float> destination = bufferOf(monoLayout, -7.0f);
    std::vector<float> wide = bufferOf(sourceLayout, -7.0f);

    EXPECT_EQ(an::extractImageChannel(nothing, an::SampleType::Float32, source.data(), sourceLayout, 0u,
                                      destination.data(), monoLayout)
                  .code(),
              an::ErrorCode::NotInitialized);
    const std::array<an::ChannelSource, 3u> sources{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout),
                                                    sourceOf(mono, monoLayout)};
    EXPECT_EQ(an::composeImageChannels(nothing, an::SampleType::Float32, sources, wide.data(), sourceLayout).code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(destination.front(), -7.0f);
    EXPECT_EQ(wide.front(), -7.0f);
}

TEST(ChannelTransferTest, AStoppedOperationRefusesToMoveAChannel)
{
    an::EngineContext engine = makeEngine();
    std::stop_source stopped;
    stopped.request_stop();
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Automatic, stopped.get_token());

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout sourceLayout = layoutOf(3u, 1u);
    const std::vector<float> source = pictureOf(sourceLayout);
    const std::vector<float> mono = pictureOf(monoLayout);
    std::vector<float> destination = bufferOf(monoLayout, -7.0f);
    std::vector<float> wide = bufferOf(sourceLayout, -7.0f);

    EXPECT_EQ(an::extractImageChannel(execution, an::SampleType::Float32, source.data(), sourceLayout, 0u,
                                      destination.data(), monoLayout)
                  .code(),
              an::ErrorCode::Cancelled);
    const std::array<an::ChannelSource, 3u> sources{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout),
                                                    sourceOf(mono, monoLayout)};
    EXPECT_EQ(an::composeImageChannels(execution, an::SampleType::Float32, sources, wide.data(), sourceLayout).code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(destination.front(), -7.0f);
    EXPECT_EQ(wide.front(), -7.0f);
}

TEST(ChannelTransferTest, TheTypedSpellingsReadTheirRepresentationOffTheirViews)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u);
    const an::ImageLayout colourLayout = layoutOf(3u, 3u);
    const std::vector<float> mono = pictureOf(monoLayout);
    std::vector<float> colour = bufferOf(colourLayout, -7.0f);
    std::vector<float> extracted = bufferOf(monoLayout, -7.0f);

    const std::optional<an::ImageView<float>> colourView =
        an::ImageView<float>::create(std::span<float>{colour}, colourLayout);
    const std::optional<an::ImageView<float>> extractedView =
        an::ImageView<float>::create(std::span<float>{extracted}, monoLayout);
    ASSERT_TRUE(colourView.has_value());
    ASSERT_TRUE(extractedView.has_value());

    const std::array<an::ChannelSource, 3u> sources{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout),
                                                    sourceOf(mono, monoLayout)};
    ASSERT_TRUE(an::composeChannels(execution, std::span<const an::ChannelSource>{sources}, *colourView).ok());
    ASSERT_TRUE(an::extractChannel(execution, colourView->constView(), 2u, *extractedView).ok());

    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            EXPECT_EQ(sampleAt(extracted, monoLayout, 0u, x, y), sampleValue(0u, x, y));
        }
    }
}

TEST(ChannelTransferTest, ExtractionAndCompositionAreTheInverseOfEachOtherOnEveryArrangement)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout monoLayout = layoutOf(1u, 1u, 1u);
    const std::array<an::ImageLayout, 3u> arrangements{layoutOf(4u, 1u), layoutOf(4u, 4u, 3u), layoutOf(4u, 2u)};
    for (const an::ImageLayout& layout : arrangements)
    {
        const std::vector<float> original = pictureOf(layout);
        std::vector<std::vector<float>> channels;
        channels.reserve(4u);
        for (std::uint32_t channel = 0u; channel < 4u; ++channel)
        {
            std::vector<float> taken = bufferOf(monoLayout, -7.0f);
            ASSERT_TRUE(an::extractImageChannel(execution, an::SampleType::Float32, original.data(), layout, channel,
                                                taken.data(), monoLayout)
                            .ok());
            channels.push_back(std::move(taken));
        }
        const std::array<an::ChannelSource, 4u> sources{
            sourceOf(channels[0], monoLayout), sourceOf(channels[1], monoLayout), sourceOf(channels[2], monoLayout),
            sourceOf(channels[3], monoLayout)};
        std::vector<float> rebuilt = bufferOf(layout, -1.0f);
        ASSERT_TRUE(an::composeImageChannels(execution, an::SampleType::Float32, sources, rebuilt.data(), layout).ok());
        EXPECT_EQ(rebuilt, original);
    }
}

TEST(ChannelTransferTest, TheReferenceAndCompiledBackendsMoveTheSameBytes)
{
    an::EngineContext engine = makeEngine();

    const an::ImageLayout monoLayout = layoutOf(1u, 1u, 2u);
    const an::ImageLayout colourLayout = layoutOf(3u, 3u, 1u);
    const std::vector<float> colour = pictureOf(colourLayout);
    const std::vector<float> mono = pictureOf(monoLayout);
    const std::array<an::ChannelSource, 3u> sources{sourceOf(mono, monoLayout), sourceOf(mono, monoLayout),
                                                    sourceOf(mono, monoLayout)};

    // One operation at a time: an execution holds a share of its engine context's thread budget for
    // as long as it exists, and this context's whole budget is one operation's worth.
    std::vector<float> scalarExtract = bufferOf(monoLayout, -7.0f);
    std::vector<float> scalarCompose = bufferOf(colourLayout, -7.0f);
    {
        an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
        ASSERT_TRUE(an::extractImageChannel(reference, an::SampleType::Float32, colour.data(), colourLayout, 1u,
                                            scalarExtract.data(), monoLayout)
                        .ok());
        ASSERT_TRUE(
            an::composeImageChannels(reference, an::SampleType::Float32, sources, scalarCompose.data(), colourLayout)
                .ok());
    }

    std::vector<float> compiledExtract = bufferOf(monoLayout, -7.0f);
    std::vector<float> compiledCompose = bufferOf(colourLayout, -7.0f);
    {
        an::ExecutionContext automatic = makeExecution(engine, an::ComputeBackend::Automatic);
        ASSERT_TRUE(an::extractImageChannel(automatic, an::SampleType::Float32, colour.data(), colourLayout, 1u,
                                            compiledExtract.data(), monoLayout)
                        .ok());
        ASSERT_TRUE(
            an::composeImageChannels(automatic, an::SampleType::Float32, sources, compiledCompose.data(), colourLayout)
                .ok());
    }

    EXPECT_EQ(scalarExtract, compiledExtract);
    EXPECT_EQ(scalarCompose, compiledCompose);
}

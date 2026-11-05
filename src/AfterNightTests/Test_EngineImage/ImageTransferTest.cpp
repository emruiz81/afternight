// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ImageTransfer.h.
//
// Copying and filling compute nothing, so what there is to get wrong is entirely at the edges: what
// the operation refuses, what it leaves alone, and what it does when it is stopped partway. The
// cases below are about those three, and the byte-for-byte agreement between the backends is next
// door in the kernel suite's differential sweep.
//
// The property most easily lost is the one about padding. A row stride wider than a row describes
// memory the image does not own the meaning of, and an implementation that filled or copied by
// buffer rather than by row would silently destroy whatever a caller had put there - a second
// image's rows, in the one case that matters.

#include <AfterNightEngine/Image/ImageTransfer.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

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

/// An operation on the engine's own choice of backend.
///
/// @param engine the context to start it on.
/// @param stopToken a stop token, for the cancellation cases.
/// @return the operation.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 std::stop_token stopToken = std::stop_token{})
{
    an::ExecutionOptions options;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// An extent, which the case has already failed if it cannot build.
///
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels.
/// @return the extent.
[[nodiscard]] an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount)
{
    const std::optional<an::ImageExtent> made =
        an::ImageExtent::create(width, height, channelCount, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A planar `Float32` layout with a stated row padding.
///
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels.
/// @param rowPaddingSamples how many samples of padding follow each row.
/// @return the layout.
[[nodiscard]] an::ImageLayout floatLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount = 1u,
                                          std::uint32_t rowPaddingSamples = 0u)
{
    const an::ImageLayoutRequest request{.extent = extentOf(width, height, channelCount),
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channelCount,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = (static_cast<std::uint64_t>(width) + rowPaddingSamples) * 4u;
    const std::optional<an::ImageLayout> made =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A buffer sized for a layout, every sample set to one value.
///
/// @param layout where the samples sit.
/// @param value what to put in every slot, padding included.
/// @return the buffer.
[[nodiscard]] std::vector<float> bufferOf(const an::ImageLayout& layout, float value)
{
    return std::vector<float>(static_cast<std::size_t>(layout.totalBytes() / 4u), value);
}

} // namespace

TEST(ImageTransferTest, ACopyReproducesEverySampleAndTouchesNoPadding)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout source = floatLayout(3u, 2u, 1u, 2u);
    const an::ImageLayout destination = floatLayout(3u, 2u, 1u, 2u);
    std::vector<float> input = bufferOf(source, 0.0f);
    for (std::size_t index = 0u; index < input.size(); ++index)
    {
        input[index] = static_cast<float>(index) + 0.5f;
    }
    std::vector<float> output = bufferOf(destination, -7.0f);

    ASSERT_TRUE(
        an::copyImageSamples(execution, an::SampleType::Float32, input.data(), source, output.data(), destination)
            .ok());

    // Row zero holds three samples and then two of padding; row one starts at offset five. The
    // buffer ends with the last row rather than with its padding, which is what a layout means by
    // its total size, so there is no trailing slot to check.
    EXPECT_EQ(output[0], input[0]);
    EXPECT_EQ(output[1], input[1]);
    EXPECT_EQ(output[2], input[2]);
    EXPECT_EQ(output[3], -7.0f);
    EXPECT_EQ(output[4], -7.0f);
    EXPECT_EQ(output[5], input[5]);
    EXPECT_EQ(output[7], input[7]);
}

TEST(ImageTransferTest, ACopyMovesEverySamplesBitsRatherThanItsValue)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = floatLayout(4u, 1u);
    std::vector<float> input = bufferOf(layout, 0.0f);
    const std::uint32_t signalling = 0x7F800001u;
    std::memcpy(input.data(), &signalling, 4u);
    input[1] = -0.0f;
    input[2] = std::numeric_limits<float>::denorm_min();
    input[3] = -std::numeric_limits<float>::infinity();

    std::vector<float> output = bufferOf(layout, 1.0f);
    ASSERT_TRUE(
        an::copyImageSamples(execution, an::SampleType::Float32, input.data(), layout, output.data(), layout).ok());

    // Compared as bits rather than as floats: a signalling pattern and a negative zero both compare
    // equal to something they are not, and this family's contract is about the bits.
    EXPECT_EQ(std::memcmp(input.data(), output.data(), layout.totalBytes()), 0);
}

TEST(ImageTransferTest, ACopyIntoAnOverlappingDestinationIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = floatLayout(4u, 2u);
    std::vector<float> shared = bufferOf(layout, 1.0f);

    const an::Status refused =
        an::copyImageSamples(execution, an::SampleType::Float32, shared.data(), layout, shared.data(), layout);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ImageTransferTest, ACopyBetweenDifferentShapesIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout source = floatLayout(4u, 2u);
    const an::ImageLayout destination = floatLayout(2u, 4u);
    std::vector<float> input = bufferOf(source, 1.0f);
    std::vector<float> output = bufferOf(destination, 0.0f);

    const an::Status refused =
        an::copyImageSamples(execution, an::SampleType::Float32, input.data(), source, output.data(), destination);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ImageTransferTest, ACopyWithAMisdescribedLayoutOrNullBufferIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = floatLayout(4u, 2u);
    std::vector<float> input = bufferOf(layout, 1.0f);
    std::vector<float> output = bufferOf(layout, 0.0f);

    const an::Status nullSource =
        an::copyImageSamples(execution, an::SampleType::Float32, nullptr, layout, output.data(), layout);
    ASSERT_TRUE(nullSource.failed());
    EXPECT_EQ(nullSource.code(), an::ErrorCode::InvalidArgument);

    // A four-byte layout does not describe two-byte samples, whatever the caller says they are.
    const an::Status wrongWidth =
        an::copyImageSamples(execution, an::SampleType::UInt16, input.data(), layout, output.data(), layout);
    ASSERT_TRUE(wrongWidth.failed());
    EXPECT_EQ(wrongWidth.code(), an::ErrorCode::InvalidArgument);
}

TEST(ImageTransferTest, AnOperationThatHoldsNothingMovesNothing)
{
    const an::ImageLayout layout = floatLayout(2u, 2u);
    std::vector<float> input = bufferOf(layout, 1.0f);
    std::vector<float> output = bufferOf(layout, 0.0f);
    const an::ExecutionContext empty;

    const an::Status copied =
        an::copyImageSamples(empty, an::SampleType::Float32, input.data(), layout, output.data(), layout);
    ASSERT_TRUE(copied.failed());
    EXPECT_EQ(copied.code(), an::ErrorCode::NotInitialized);

    const float value = 3.0f;
    const an::Status filled = an::fillImageSamples(empty, an::SampleType::Float32, &value, output.data(), layout);
    ASSERT_TRUE(filled.failed());
    EXPECT_EQ(filled.code(), an::ErrorCode::NotInitialized);
}

TEST(ImageTransferTest, AStoppedOperationRefusesToMoveAnything)
{
    an::EngineContext engine = makeEngine();
    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext execution = makeExecution(engine, stop.get_token());

    const an::ImageLayout layout = floatLayout(64u, 64u);
    std::vector<float> input = bufferOf(layout, 1.0f);
    std::vector<float> output = bufferOf(layout, 0.0f);

    const an::Status copied =
        an::copyImageSamples(execution, an::SampleType::Float32, input.data(), layout, output.data(), layout);
    ASSERT_TRUE(copied.failed());
    EXPECT_EQ(copied.code(), an::ErrorCode::Cancelled);

    const float value = 3.0f;
    const an::Status filled = an::fillImageSamples(execution, an::SampleType::Float32, &value, output.data(), layout);
    ASSERT_TRUE(filled.failed());
    EXPECT_EQ(filled.code(), an::ErrorCode::Cancelled);
}

TEST(ImageTransferTest, AFillWritesEverySampleAndNoPadding)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = floatLayout(3u, 2u, 1u, 2u);
    std::vector<float> samples = bufferOf(layout, -7.0f);
    const float value = 2.5f;

    ASSERT_TRUE(an::fillImageSamples(execution, an::SampleType::Float32, &value, samples.data(), layout).ok());

    EXPECT_EQ(samples[0], 2.5f);
    EXPECT_EQ(samples[2], 2.5f);
    EXPECT_EQ(samples[3], -7.0f);
    EXPECT_EQ(samples[4], -7.0f);
    EXPECT_EQ(samples[5], 2.5f);
    EXPECT_EQ(samples[7], 2.5f);
}

TEST(ImageTransferTest, AFillRepeatsTheBitsItWasGiven)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = floatLayout(9u, 2u);
    std::vector<float> samples = bufferOf(layout, 0.0f);

    const std::uint32_t signalling = 0x7FA00001u;
    float pattern = 0.0f;
    std::memcpy(&pattern, &signalling, 4u);

    ASSERT_TRUE(an::fillImageSamples(execution, an::SampleType::Float32, &pattern, samples.data(), layout).ok());

    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &samples[index], 4u);
        EXPECT_EQ(bits, signalling) << "sample " << index;
    }
}

TEST(ImageTransferTest, AFillFromAValueInsideTheImageIsRefused)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = floatLayout(4u, 2u);
    std::vector<float> samples = bufferOf(layout, 1.0f);

    const an::Status refused =
        an::fillImageSamples(execution, an::SampleType::Float32, samples.data() + 3u, samples.data(), layout);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ImageTransferTest, TheTypedSpellingsReadTheirRepresentationOffTheirViews)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = floatLayout(4u, 2u);
    std::vector<float> input(8u, 0.0f);
    std::vector<float> output(8u, 0.0f);
    for (std::size_t index = 0u; index < input.size(); ++index)
    {
        input[index] = static_cast<float>(index);
    }

    const std::optional<an::ImageView<const float>> sourceView =
        an::ImageView<const float>::create(std::span<const float>{input}, layout);
    const std::optional<an::ImageView<float>> destinationView =
        an::ImageView<float>::create(std::span<float>{output}, layout);
    ASSERT_TRUE(sourceView.has_value());
    ASSERT_TRUE(destinationView.has_value());

    ASSERT_TRUE(an::copyImage(execution, *sourceView, *destinationView).ok());
    EXPECT_EQ(output[7], 7.0f);

    ASSERT_TRUE(an::fillImage(execution, *destinationView, 4.25f).ok());
    EXPECT_EQ(output[0], 4.25f);
    EXPECT_EQ(output[7], 4.25f);

    ASSERT_TRUE(an::clearImage(execution, *destinationView).ok());
    EXPECT_EQ(output[0], 0.0f);
    EXPECT_EQ(output[7], 0.0f);
}

TEST(ImageTransferTest, AClearZeroesEverySampleAndNoPadding)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    // Two channels, so the plane padding is exercised as well as the row padding: a clear that wrote
    // by buffer rather than by row would cover both, and only a two-plane case shows the second.
    const an::ImageLayout layout = floatLayout(3u, 2u, 2u, 2u);
    std::vector<float> samples = bufferOf(layout, 5.5f);

    ASSERT_TRUE(an::clearImageSamples(execution, an::SampleType::Float32, samples.data(), layout).ok());

    for (std::uint32_t plane = 0u; plane < 2u; ++plane)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            const std::optional<std::uint64_t> rowOffset = layout.rowByteOffset(plane, y);
            ASSERT_TRUE(rowOffset.has_value());
            const std::size_t first = static_cast<std::size_t>(*rowOffset / 4u);
            for (std::size_t index = 0u; index < 3u; ++index)
            {
                EXPECT_EQ(samples[first + index], 0.0f) << "plane " << plane << " row " << y << " sample " << index;
            }
            // The final row's padding is past what the layout describes and so is not in the buffer
            // at all; every other row's is, and every one of those must still hold what it held.
            for (std::size_t index = 3u; index < 5u && first + index < samples.size(); ++index)
            {
                EXPECT_EQ(samples[first + index], 5.5f) << "plane " << plane << " row " << y << " padding " << index;
            }
        }
    }
}

TEST(ImageTransferTest, AClearIsWhatTheRepresentationCallsZeroInEveryRepresentation)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    // Every resident representation, cleared through the untyped entry point a caller with a
    // run-time sample type reaches. Each is checked as bytes rather than as a value, because that is
    // what a clear promises: the pattern a reader of any of these representations calls zero.
    constexpr std::array<an::SampleType, 8u> kAll{
        an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
        an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};
    for (const an::SampleType sampleType : kAll)
    {
        const std::uint32_t elementSize = an::sampleTypeSizeBytes(sampleType);
        const std::optional<an::ImageExtent> extent = an::ImageExtent::create(7u, 3u, 1u, an::defaultEngineLimits());
        ASSERT_TRUE(extent.has_value());
        const std::optional<an::ImageLayout> layout =
            an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                           .elementSizeBytes = elementSize,
                                                           .samplesPerPixel = 1u,
                                                           .planeCount = 1u,
                                                           .rowAlignmentBytes = 1u},
                                    an::defaultEngineLimits());
        ASSERT_TRUE(layout.has_value());

        std::vector<std::uint8_t> samples(static_cast<std::size_t>(layout->totalBytes()), 0xA5u);
        ASSERT_TRUE(an::clearImageSamples(execution, sampleType, samples.data(), *layout).ok())
            << an::sampleTypeName(sampleType);
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            EXPECT_EQ(samples[index], 0u) << an::sampleTypeName(sampleType) << " byte " << index;
        }
    }
}

TEST(ImageTransferTest, AClearOfAMisdescribedLayoutOrNullBufferIsRefused)
{
    an::EngineContext engine = makeEngine();

    const an::ImageLayout layout = floatLayout(4u, 2u);
    std::vector<float> samples = bufferOf(layout, 1.0f);

    // One operation at a time: an execution holds a share of its engine context's thread budget for
    // as long as it exists, and this context's whole budget is one operation's worth.
    {
        an::ExecutionContext execution = makeExecution(engine);
        EXPECT_EQ(an::clearImageSamples(execution, an::SampleType::Float32, nullptr, layout).code(),
                  an::ErrorCode::InvalidArgument);
        EXPECT_EQ(an::clearImageSamples(execution, an::SampleType::UInt16, samples.data(), layout).code(),
                  an::ErrorCode::InvalidArgument);
    }
    EXPECT_EQ(samples.front(), 1.0f);

    const an::ExecutionContext empty;
    EXPECT_EQ(an::clearImageSamples(empty, an::SampleType::Float32, samples.data(), layout).code(),
              an::ErrorCode::NotInitialized);

    std::stop_source stop;
    stop.request_stop();
    an::ExecutionContext stopped = makeExecution(engine, stop.get_token());
    EXPECT_EQ(an::clearImageSamples(stopped, an::SampleType::Float32, samples.data(), layout).code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(samples.front(), 1.0f);
}

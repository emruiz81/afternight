// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What the whole-image operations act on: the picture that is described, never the memory that is
// held.
//
// A storage in this engine carries two byte counts. One says how much was allocated and the other
// says how much the current layout describes, and they stop being the same number the moment a
// storage is re-described over memory it keeps. Everything that walks an image therefore has two
// candidate extents available to it and only one of them is the picture, and choosing the wrong one
// is not a crash: it is an operation that costs time proportional to an allocation, overwrites bytes
// a caller may still be using, or reads a sample from where it used to be.
//
// The neighbouring storage cases prove the property for the storage type's own single-threaded fill.
// These prove it for the operations, which are a different implementation entirely: they resolve a
// kernel through the dispatch table, split the work across the operation's threads, and derive every
// address from the layout they were handed. Nothing in the type's fill would catch a walk that took
// its row count from `capacityBytes`.
//
// Each case writes a marker over the whole allocation first, shrinks the description, runs one
// operation, and then restores the full description - because restoring it is the only way to look
// at the retained tail at all, which is itself part of what is being asserted.

#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/ImageTransfer.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace
{

/// The value the whole allocation is marked with before anything runs.
constexpr std::uint16_t kRetained = 0xA5A5u;

/// How wide and tall the allocation is.
constexpr std::uint32_t kAllocatedSide = 32u;

/// How wide and tall the description is after it is shrunk.
constexpr std::uint32_t kDescribedSide = 12u;

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
/// @return the operation.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// A tightly packed planar layout of 16-bit samples.
///
/// @param side the width and the height.
/// @param channelCount how many channels, one plane each.
/// @return the layout.
[[nodiscard]] an::ImageLayout planarLayout(std::uint32_t side, std::uint32_t channelCount = 1u)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(side, side, channelCount, limits);
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(
        an::ImageLayoutRequest{.extent = extent.value(),
                               .elementSizeBytes = static_cast<std::uint32_t>(sizeof(std::uint16_t)),
                               .samplesPerPixel = 1u,
                               .planeCount = channelCount,
                               .rowAlignmentBytes = 1u},
        limits);
    EXPECT_TRUE(layout.has_value());
    return layout.value();
}

/// A storage of the allocated shape whose every sample holds the retained marker.
///
/// @param engine the context that owns the memory.
/// @param channelCount how many channels the allocation has.
/// @return the storage, valid unless the case has already failed.
[[nodiscard]] an::Result<an::ImageStorage<std::uint16_t>> markedStorage(const an::EngineContext& engine,
                                                                        std::uint32_t channelCount = 1u)
{
    an::Result<an::ImageStorage<std::uint16_t>> storage = an::ImageStorage<std::uint16_t>::create(
        engine, planarLayout(kAllocatedSide, channelCount), an::StorageInitialization::Uninitialized);
    EXPECT_TRUE(storage.hasValue());
    if (storage.hasValue())
    {
        EXPECT_TRUE(storage->fill(kRetained).ok());
    }
    return storage;
}

/// How many samples a layout describes, counting every plane's.
///
/// @param layout the geometry.
/// @return the count.
[[nodiscard]] std::uint64_t describedSamples(const an::ImageLayout& layout)
{
    return layout.totalBytes() / sizeof(std::uint16_t);
}

} // namespace

TEST(LogicalExtentOperationsTest, AClearCoversTheDescribedPictureAndNotTheAllocation)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    an::Result<an::ImageStorage<std::uint16_t>> storage = markedStorage(engine);
    ASSERT_TRUE(storage.hasValue());

    const an::ImageLayout allocated = planarLayout(kAllocatedSide);
    const an::ImageLayout described = planarLayout(kDescribedSide);
    ASSERT_TRUE(storage->reshape(described).ok());
    ASSERT_EQ(storage->logicalBytes(), described.totalBytes());
    ASSERT_EQ(storage->capacityBytes(), allocated.totalBytes());

    const std::optional<an::ImageView<std::uint16_t>> view = storage->view();
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(an::clearImage(execution, *view).ok());

    // The two counts are still separate afterwards: an operation describes nothing and allocates
    // nothing, so neither number is its to move.
    EXPECT_EQ(storage->logicalBytes(), described.totalBytes());
    EXPECT_EQ(storage->capacityBytes(), allocated.totalBytes());

    ASSERT_TRUE(storage->reshape(allocated).ok());
    const std::uint16_t* const samples = storage->data();
    for (std::uint64_t index = 0u; index < describedSamples(described); ++index)
    {
        ASSERT_EQ(samples[index], 0u) << "cleared sample " << index;
    }
    for (std::uint64_t index = describedSamples(described); index < describedSamples(allocated); ++index)
    {
        ASSERT_EQ(samples[index], kRetained) << "retained sample " << index;
    }
}

TEST(LogicalExtentOperationsTest, AFillAndACopyCoverTheDescribedPictureAndNotTheAllocation)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    an::Result<an::ImageStorage<std::uint16_t>> storage = markedStorage(engine);
    ASSERT_TRUE(storage.hasValue());

    const an::ImageLayout allocated = planarLayout(kAllocatedSide);
    const an::ImageLayout described = planarLayout(kDescribedSide);
    ASSERT_TRUE(storage->reshape(described).ok());

    const std::optional<an::ImageView<std::uint16_t>> view = storage->view();
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(an::fillImage(execution, *view, std::uint16_t{0x1234u}).ok());

    // Restoring the full description is the only way to see the tail, so each operation is checked
    // in turn rather than one after the other over the same bytes.
    ASSERT_TRUE(storage->reshape(allocated).ok());
    for (std::uint64_t index = 0u; index < describedSamples(described); ++index)
    {
        ASSERT_EQ(storage->data()[index], 0x1234u) << "filled sample " << index;
    }
    for (std::uint64_t index = describedSamples(described); index < describedSamples(allocated); ++index)
    {
        ASSERT_EQ(storage->data()[index], kRetained) << "retained sample " << index;
    }

    // And the same for a copy, whose destination is the re-described storage: the copy is sized by
    // the description on both sides, so a source of the described shape is the whole of what fits.
    ASSERT_TRUE(storage->reshape(described).ok());
    const std::vector<std::uint16_t> source(static_cast<std::size_t>(describedSamples(described)), 0x4321u);
    ASSERT_TRUE(
        an::copyImageSamples(execution, an::SampleType::UInt16, source.data(), described, storage->data(), described)
            .ok());

    ASSERT_TRUE(storage->reshape(allocated).ok());
    for (std::uint64_t index = 0u; index < describedSamples(described); ++index)
    {
        ASSERT_EQ(storage->data()[index], 0x4321u) << "copied sample " << index;
    }
    for (std::uint64_t index = describedSamples(described); index < describedSamples(allocated); ++index)
    {
        ASSERT_EQ(storage->data()[index], kRetained) << "retained sample " << index;
    }
}

TEST(LogicalExtentOperationsTest, AnExtractionReadsTheDescribedPicturesStridesAndNotTheAllocatedOnes)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout allocated = planarLayout(kAllocatedSide, 3u);
    const an::ImageLayout described = planarLayout(kDescribedSide, 3u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, allocated, an::StorageInitialization::Uninitialized);
    ASSERT_TRUE(storage.hasValue());

    // Every sample of the allocation gets a distinct value, so a read from the wrong plane or the
    // wrong row is a wrong number rather than a coincidence. Where a channel starts moves when the
    // description shrinks - a plane of the small picture is a fraction of a plane of the large one -
    // which is precisely what an operation reading the allocated geometry would get wrong.
    std::uint16_t* const samples = storage->data();
    for (std::uint64_t index = 0u; index < describedSamples(allocated); ++index)
    {
        samples[index] = static_cast<std::uint16_t>(index + 1u);
    }

    ASSERT_TRUE(storage->reshape(described).ok());
    const an::ImageLayout monoLayout = planarLayout(kDescribedSide);
    std::vector<std::uint16_t> taken(static_cast<std::size_t>(describedSamples(monoLayout)), 0u);
    ASSERT_TRUE(an::extractImageChannel(execution, an::SampleType::UInt16, storage->data(), described, 1u, taken.data(),
                                        monoLayout)
                    .ok());

    for (std::uint32_t y = 0u; y < kDescribedSide; ++y)
    {
        for (std::uint32_t x = 0u; x < kDescribedSide; ++x)
        {
            const std::optional<std::uint64_t> sourceOffset = described.sampleByteOffset(1u, y, x, 0u);
            const std::optional<std::uint64_t> takenOffset = monoLayout.sampleByteOffset(0u, y, x, 0u);
            ASSERT_TRUE(sourceOffset.has_value());
            ASSERT_TRUE(takenOffset.has_value());
            ASSERT_EQ(taken[static_cast<std::size_t>(*takenOffset / sizeof(std::uint16_t))],
                      samples[*sourceOffset / sizeof(std::uint16_t)])
                << "column " << x << " row " << y;
        }
    }
}

TEST(LogicalExtentOperationsTest, ACompositionWritesTheDescribedPictureAndNotTheAllocation)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    an::Result<an::ImageStorage<std::uint16_t>> storage = markedStorage(engine, 3u);
    ASSERT_TRUE(storage.hasValue());

    const an::ImageLayout allocated = planarLayout(kAllocatedSide, 3u);
    const an::ImageLayout described = planarLayout(kDescribedSide, 3u);
    ASSERT_TRUE(storage->reshape(described).ok());

    const an::ImageLayout monoLayout = planarLayout(kDescribedSide);
    const std::vector<std::uint16_t> channel(static_cast<std::size_t>(describedSamples(monoLayout)), 0x0777u);
    const std::array<an::ChannelSource, 3u> sources{
        an::ChannelSource{.samples = channel.data(), .layout = &monoLayout},
        an::ChannelSource{.samples = channel.data(), .layout = &monoLayout},
        an::ChannelSource{.samples = channel.data(), .layout = &monoLayout}};
    ASSERT_TRUE(an::composeImageChannels(execution, an::SampleType::UInt16, sources, storage->data(), described).ok());

    EXPECT_EQ(storage->logicalBytes(), described.totalBytes());
    EXPECT_EQ(storage->capacityBytes(), allocated.totalBytes());

    ASSERT_TRUE(storage->reshape(allocated).ok());
    const std::uint16_t* const samples = storage->data();
    for (std::uint64_t index = 0u; index < describedSamples(described); ++index)
    {
        ASSERT_EQ(samples[index], 0x0777u) << "composed sample " << index;
    }
    for (std::uint64_t index = describedSamples(described); index < describedSamples(allocated); ++index)
    {
        ASSERT_EQ(samples[index], kRetained) << "retained sample " << index;
    }
}

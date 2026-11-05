// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Tests for three storage defects a storage type invites.
//
// Each is closed here by construction rather than by a fix, which is a claim that has to be checked
// rather than asserted - "the type makes it impossible" is exactly the sentence that stops anybody
// looking.
//
// **A description can be enlarged over memory that was not enlarged.** A re-description that takes a
// boolean saying whether to reallocate, sets the new width, height and channel count
// unconditionally, and then re-derives its per-channel pointers from the *new* geometry over the
// *old* allocation produces, with the boolean false, a set of pointers past the end of the buffer -
// silently, on a call whose stated purpose is to change a description.
//
// **What is cleared, and what is left behind.** A storage that keeps a logical size and a separate
// allocated size has the two read by different callers. Whether the retained tail between them is
// ever readable is the same question as the one above, and it cannot be answered without a stated
// policy. This engine has one, and these cases are it.
//
// **Retained bytes an object does not own.** An image built over somebody else's buffer still
// reports an allocated size, so an accounting sweep counts memory it cannot free and a peak figure
// includes memory nobody allocated.
//
// The cases below are written as the defect, not as the fix: each one does the thing that goes
// wrong and asserts that this engine refuses it, bounds it, or reports it honestly.

#include <AfterNightEngine/Image/ImageStorage.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

/// An engine context with a stated memory budget.
///
/// @param budgetBytes what the context may have outstanding at once.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint64_t budgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.limits = an::defaultEngineLimits();
    options.limits.memoryBudgetBytes = budgetBytes;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// A validated layout for a tightly packed planar frame of 16-bit samples.
///
/// @param width the frame's width.
/// @param height the frame's height.
/// @param channelCount how many channels, one plane each.
/// @return the layout.
[[nodiscard]] an::ImageLayout planarLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount = 1u)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channelCount, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = extent.value(),
                                         .elementSizeBytes = static_cast<std::uint32_t>(sizeof(std::uint16_t)),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channelCount,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    EXPECT_TRUE(layout.has_value());
    return layout.value();
}

/// The last byte any sample of `layout` occupies, one past the end.
///
/// Walks the geometry the way a caller deriving a pointer would, rather than trusting the total: the
/// defect being closed is precisely a total that agreed with nobody's arithmetic.
///
/// @param layout the geometry to walk.
/// @return the offset one past the furthest sample byte, or zero when the layout names no sample.
[[nodiscard]] std::uint64_t furthestSampleEnd(const an::ImageLayout& layout)
{
    std::uint64_t furthest = 0u;
    for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            const std::optional<std::uint64_t> offset =
                layout.sampleByteOffset(plane, y, layout.extent().width() - 1u, layout.samplesPerPixel() - 1u);
            EXPECT_TRUE(offset.has_value());
            if (offset.has_value())
            {
                furthest = (std::max)(furthest, *offset + layout.elementSizeBytes());
            }
        }
    }
    return furthest;
}

} // namespace

TEST(StorageGrowthTest, ThereIsNoWayToAskForAReshapeThatGrowsWhatIsHeld)
{
    // The defective call is `redefine(width, height, channels, ..., reallocateIfNeeded)`, and the
    // harm is entirely in the last argument: with it false, the description grows and the memory
    // does not. Closing that means the argument cannot be spelled, which is a property of the
    // signature rather than of any run, so it is checked where signatures are checked.
    using Reshape = decltype(&an::ImageStorageBytes::reshape);
    static_assert(std::is_invocable_r_v<an::Status, Reshape, an::ImageStorageBytes&, const an::ImageLayout&>,
                  "a reshape is a layout and nothing else");
    static_assert(!std::is_invocable_v<Reshape, an::ImageStorageBytes&, const an::ImageLayout&, bool>,
                  "no overload takes a flag that would let a description grow past its memory");
    static_assert(!std::is_invocable_v<Reshape, an::ImageStorageBytes&, std::uint32_t, std::uint32_t, std::uint32_t>,
                  "a reshape states a validated geometry, not three loose numbers");

    // The same three hold for the typed storage a caller actually handles.
    using TypedReshape = decltype(&an::ImageStorage<std::uint16_t>::reshape);
    static_assert(
        std::is_invocable_r_v<an::Status, TypedReshape, an::ImageStorage<std::uint16_t>&, const an::ImageLayout&>);
    static_assert(!std::is_invocable_v<TypedReshape, an::ImageStorage<std::uint16_t>&, const an::ImageLayout&, bool>);

    // A run-time assertion so this case reaches the count rather than passing silently at compile
    // time: a suite whose only content is a static assertion is a suite nobody notices deleting.
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, planarLayout(8u, 8u), an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());
    EXPECT_TRUE(storage->reshape(planarLayout(8u, 8u)).ok());
}

TEST(StorageGrowthTest, TheBoundaryIsExactlyWhatIsHeldAndOneRowMoreIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout allocated = planarLayout(16u, 16u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, allocated, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());

    // One row fewer fits, the exact size fits, and one row more does not. The interesting one is the
    // last: a check written as "much bigger" passes for a geometry that is over by a single row,
    // which is the shape a real re-description has.
    const an::ImageLayout oneRowFewer = planarLayout(16u, 15u);
    const an::ImageLayout oneRowMore = planarLayout(16u, 17u);
    ASSERT_EQ(oneRowMore.totalBytes(), allocated.totalBytes() + allocated.rowStrideBytes());

    EXPECT_TRUE(storage->reshape(oneRowFewer).ok());
    EXPECT_TRUE(storage->reshape(allocated).ok());

    const an::Status refused = storage->reshape(oneRowMore);
    EXPECT_EQ(refused.code(), an::ErrorCode::OutOfRange);
    const an::StatusContextField* const requested = refused.findContext(an::ContextKey::Requested);
    const an::StatusContextField* const available = refused.findContext(an::ContextKey::Available);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(requested->count, oneRowMore.totalBytes());
    EXPECT_EQ(available->count, allocated.totalBytes());
    EXPECT_EQ(storage->layout(), allocated);
}

TEST(StorageGrowthTest, NoSampleAnyLayoutTheStorageAcceptsNamesLiesOutsideTheAllocation)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout allocated = planarLayout(24u, 24u, 3u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, allocated, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());
    const std::uint64_t capacity = storage->capacityBytes();

    // Every shape a caller might re-describe this memory as, including ones with more planes and
    // fewer rows than it was allocated with. The one that does not fit is refused; every one that is
    // accepted has its furthest sample inside the allocation, which is the property the current
    // engine loses when its channel pointers are re-derived over an allocation that did not grow.
    const an::ImageLayout shapes[] = {planarLayout(24u, 24u, 3u), planarLayout(12u, 12u, 3u),
                                      planarLayout(72u, 24u, 1u), planarLayout(24u, 8u, 9u),
                                      // One row over, which is the size a real re-description
                                      // overshoots by. A bound that is only checked coarsely admits
                                      // this and puts the last row past the end.
                                      planarLayout(72u, 25u, 1u), planarLayout(24u, 25u, 3u)};
    for (const an::ImageLayout& shape : shapes)
    {
        const an::Status reshaped = storage->reshape(shape);
        if (reshaped.failed())
        {
            EXPECT_GT(shape.totalBytes(), capacity);
            continue;
        }
        EXPECT_LE(furthestSampleEnd(shape), capacity);
        EXPECT_LE(furthestSampleEnd(*storage->layout()), storage->capacityBytes());

        const std::optional<an::ImageView<std::uint16_t>> view = storage->view();
        ASSERT_TRUE(view.has_value());
        EXPECT_EQ(view->layout().totalBytes(), storage->logicalBytes());
    }
}

TEST(StorageTailTest, WhatIsWrittenIsWhatTheStorageMeansAndNotWhatItHolds)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout allocated = planarLayout(32u, 32u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, allocated, an::StorageInitialization::Uninitialized);
    ASSERT_TRUE(storage.hasValue());
    ASSERT_TRUE(storage->fill(0xa5a5u).ok());

    const an::ImageLayout smaller = planarLayout(16u, 16u);
    ASSERT_TRUE(storage->reshape(smaller).ok());
    // The two numbers are separate, which is the whole of the policy: one says what the samples are,
    // the other says what is held on their behalf.
    EXPECT_EQ(storage->logicalBytes(), smaller.totalBytes());
    EXPECT_EQ(storage->capacityBytes(), allocated.totalBytes());
    EXPECT_LT(storage->logicalBytes(), storage->capacityBytes());

    ASSERT_TRUE(storage->fill(0x1234u).ok());

    // Back to the full shape, which is the only way to look at the tail at all. Everything the
    // smaller shape named holds the second value; everything past it still holds the first, because
    // a fill covers what the storage describes and a retained tail is not described.
    ASSERT_TRUE(storage->reshape(allocated).ok());
    const std::uint64_t writtenSamples = smaller.totalBytes() / sizeof(std::uint16_t);
    const std::uint16_t* const samples = storage->data();
    for (std::uint64_t index = 0u; index < writtenSamples; ++index)
    {
        ASSERT_EQ(samples[index], 0x1234u) << "sample " << index;
    }
    for (std::uint64_t index = writtenSamples; index < allocated.totalBytes() / sizeof(std::uint16_t); ++index)
    {
        ASSERT_EQ(samples[index], 0xa5a5u) << "retained sample " << index;
    }
}

TEST(StorageTailTest, ARetainedTailIsUnreachableThroughEveryPathAStorageOffers)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout allocated = planarLayout(32u, 32u, 2u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, allocated, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());

    const an::ImageLayout smaller = planarLayout(8u, 8u, 2u);
    ASSERT_TRUE(storage->reshape(smaller).ok());

    // A view describes the samples, not the allocation. Its rows stop at the row's samples and its
    // planes stop at the plane count the layout names, so there is no index into it that reaches the
    // retained bytes - which is what makes "is the tail readable" a question with an answer.
    const std::optional<an::ImageView<std::uint16_t>> view = storage->view();
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->planeCount(), smaller.planeCount());
    EXPECT_EQ(view->layout().totalBytes(), smaller.totalBytes());
    EXPECT_FALSE(view->row(smaller.planeCount(), 0u).has_value());
    EXPECT_FALSE(view->row(0u, smaller.extent().height()).has_value());
    EXPECT_EQ(furthestSampleEnd(*storage->layout()), smaller.totalBytes());
    EXPECT_EQ(storage->sampleCapacity(), allocated.totalBytes() / sizeof(std::uint16_t));
}

TEST(StorageOwnershipTest, BorrowedBytesAreNeverCountedAgainstTheContextThatDidNotAllocateThem)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t idle = resource->counters().liveBytes;

    std::vector<std::byte> foreign(4096u, std::byte{0x7f});
    an::Result<an::SampleBuffer> borrowed = an::SampleBuffer::borrow(std::span<std::byte>(foreign));
    ASSERT_TRUE(borrowed.hasValue());

    // The buffer describes four thousand bytes and the engine has allocated none of them. Were these
    // two numbers one field, a sweep over live images would count memory it neither owns nor can
    // release.
    EXPECT_EQ(borrowed->byteCount(), foreign.size());
    EXPECT_EQ(borrowed->ownership(), an::SampleBufferOwnership::Borrowed);
    EXPECT_EQ(borrowed->contextId(), 0u);
    EXPECT_FALSE(borrowed->adoptable());
    EXPECT_EQ(resource->counters().liveBytes, idle);
    EXPECT_EQ(resource->counters().peakBytes, idle);

    // And the ownership is what refuses the hand-over, by name, so a caller knows it needs a copy
    // rather than discovering later that it freed something it did not allocate.
    an::Result<an::ImageStorageBytes> taken = borrowed->releaseOwned();
    ASSERT_FALSE(taken.hasValue());
    EXPECT_EQ(taken.status().code(), an::ErrorCode::InvalidState);
}

TEST(StorageOwnershipTest, MemoryReleasedThroughSomebodyElsesTokenIsNotTheEnginesEither)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t idle = resource->counters().liveBytes;

    auto decoded = std::make_shared<std::vector<std::byte>>(2048u, std::byte{0x11});
    an::Result<an::SampleBuffer> external = an::SampleBuffer::external(an::SampleBufferOwnership::DecoderOwned,
                                                                       decoded->data(), decoded->size(), decoded, true);
    ASSERT_TRUE(external.hasValue());

    EXPECT_EQ(external->byteCount(), decoded->size());
    EXPECT_EQ(external->contextId(), 0u);
    EXPECT_FALSE(external->adoptable());
    EXPECT_EQ(resource->counters().liveBytes, idle);
}

TEST(StorageOwnershipTest, EngineMemoryReportsTheContextItCameFromAndGivesItBack)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t idle = resource->counters().liveBytes;

    const an::ImageLayout layout = planarLayout(32u, 32u);
    {
        an::Result<an::ImageStorageBytes> owned =
            an::ImageStorageBytes::create(engine, layout, an::StorageInitialization::Zeroed);
        ASSERT_TRUE(owned.hasValue());

        // Owned memory names its context, which is what makes "these bytes are ours" a fact rather
        // than an inference from a boolean somebody set when the object was built.
        EXPECT_NE(owned->contextId(), 0u);
        EXPECT_EQ(owned->memoryResource(), resource);
        EXPECT_EQ(resource->counters().liveBytes, idle + layout.totalBytes());

        an::SampleBuffer adopted = an::SampleBuffer::adopt(std::move(*owned));
        EXPECT_EQ(adopted.ownership(), an::SampleBufferOwnership::Owned);
        EXPECT_TRUE(adopted.adoptable());
        EXPECT_NE(adopted.contextId(), 0u);

        // A storage that has handed its memory over holds nothing and claims nothing, rather than
        // still reporting the size it used to have.
        EXPECT_FALSE(owned->valid());
        EXPECT_EQ(owned->capacityBytes(), 0u);
        EXPECT_EQ(owned->logicalBytes(), 0u);
        EXPECT_EQ(owned->contextId(), 0u);
    }

    EXPECT_EQ(resource->counters().liveBytes, idle);
}

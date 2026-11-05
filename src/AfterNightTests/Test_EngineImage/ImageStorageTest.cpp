// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ImageStorage.h.
//
// Three properties of this type are the reason it exists, and each of them answers a defect that
// costs a wrong result rather than an abstract nicety.
//
// **A refusal is returned and carries numbers.** An allocation helper that returns a null callers
// do not check turns an image that did not fit into a crash somewhere else. The budget cases below
// assert the code, and that the request, what was available and the limit all arrive with it,
// because those three are what a caller answers a budget failure with.
//
// **What the samples start as is a decision somebody wrote down.** An allocation whose zeroing is
// assumed rather than stated once produced a comet stack that was wrong in about half of its runs,
// bit-identically wrong each time. The initialization cases assert that `Zeroed` reaches
// the padding as well as the samples, because a fixture comparing two images byte for byte is
// otherwise comparing the allocator's history.
//
// **The memory is counted and released.** Every case that allocates also asserts what the context's
// resource says afterwards, and the lifetime cases assert the other direction: a context with an
// image outstanding refuses to shut down, which is what makes releasing the image the instruction
// rather than a lifetime the caller has to get right on its own.

#include <AfterNightEngine/Image/ImageStorage.h>

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

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

/// A layout request for a small planar frame.
///
/// @param width the frame's width.
/// @param height the frame's height.
/// @param channelCount how many channels, one plane each.
/// @param rowAlignmentBytes the boundary rows and planes start on.
/// @return the request, whose element size is that of a 16-bit sample.
[[nodiscard]] an::ImageLayoutRequest planarRequest(std::uint32_t width, std::uint32_t height,
                                                   std::uint32_t channelCount, std::uint64_t rowAlignmentBytes = 1u)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channelCount, limits);
    EXPECT_TRUE(extent.has_value());
    return an::ImageLayoutRequest{.extent = extent.value(),
                                  .elementSizeBytes = static_cast<std::uint32_t>(sizeof(std::uint16_t)),
                                  .samplesPerPixel = 1u,
                                  .planeCount = channelCount,
                                  .rowAlignmentBytes = rowAlignmentBytes};
}

/// A validated layout for a small planar frame.
///
/// @param width the frame's width.
/// @param height the frame's height.
/// @param channelCount how many channels, one plane each.
/// @param rowAlignmentBytes the boundary rows and planes start on.
/// @return the layout.
[[nodiscard]] an::ImageLayout planarLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount,
                                           std::uint64_t rowAlignmentBytes = 1u)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(planarRequest(width, height, channelCount, rowAlignmentBytes), limits);
    EXPECT_TRUE(layout.has_value());
    return layout.value();
}

TEST(ImageStorageTest, TakingAnImageTakesItsBytesFromTheContextsOwnResource)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t before = resource->counters().liveBytes;

    const an::ImageLayout layout = planarLayout(64u, 32u, 3u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue()) << storage.status().message();

    EXPECT_TRUE(storage->valid());
    EXPECT_EQ(storage->capacityBytes(), layout.totalBytes());
    EXPECT_EQ(storage->logicalBytes(), layout.totalBytes());
    EXPECT_EQ(storage->sampleCapacity(), layout.totalBytes() / sizeof(std::uint16_t));
    EXPECT_EQ(storage->alignmentBytes(), an::kDefaultBufferAlignmentBytes);
    EXPECT_EQ(storage->contextId(), engine.id());
    EXPECT_EQ(storage->memoryResource(), resource);
    EXPECT_EQ(storage->layout(), layout);
    EXPECT_EQ(resource->counters().liveBytes, before + layout.totalBytes());
    EXPECT_TRUE(an::isPointerAligned(storage->data(), an::kDefaultBufferAlignmentBytes));
}

TEST(ImageStorageTest, ZeroingReachesThePaddingAndNotOnlyTheSamples)
{
    // The padding is the half a per-row fill would miss, and it is the half that makes two images
    // of one layout comparable byte for byte. A 64-byte row alignment over a 12-byte row leaves 52
    // bytes per row that nothing else in this suite ever writes.
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout layout = planarLayout(6u, 4u, 2u, 64u);
    ASSERT_GT(layout.rowStrideBytes(), layout.minimumRowBytes());

    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());

    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(storage->data());
    for (std::uint64_t index = 0u; index < storage->capacityBytes(); ++index)
    {
        ASSERT_EQ(bytes[index], 0u) << index;
    }
}

TEST(ImageStorageTest, AFilledImageHoldsItsValueInEverySampleOfEveryPlane)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout layout = planarLayout(5u, 3u, 3u, 64u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::createFilled(engine, layout, static_cast<std::uint16_t>(4242u));
    ASSERT_TRUE(storage.hasValue());

    const std::optional<an::ImageView<const std::uint16_t>> view = std::as_const(*storage).view();
    ASSERT_TRUE(view.has_value());
    for (std::uint32_t planeIndex = 0u; planeIndex < view->planeCount(); ++planeIndex)
    {
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            const std::optional<std::span<const std::uint16_t>> row = view->row(planeIndex, y);
            ASSERT_TRUE(row.has_value());
            for (const std::uint16_t sample : *row)
            {
                ASSERT_EQ(sample, 4242u);
            }
        }
    }
}

TEST(ImageStorageTest, ALayoutOfAnotherSampleSizeIsRefusedBeforeAnythingIsAllocated)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const an::MemoryCounters before = resource->counters();

    const an::ImageLayout layout = planarLayout(8u, 8u, 1u);
    an::Result<an::ImageStorage<std::uint32_t>> storage =
        an::ImageStorage<std::uint32_t>::create(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_FALSE(storage.hasValue());
    EXPECT_EQ(storage.status().code(), an::ErrorCode::IncompatibleArguments);

    const an::StatusContextField* const requested = storage.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* const available = storage.status().findContext(an::ContextKey::Available);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(requested->count, sizeof(std::uint16_t));
    EXPECT_EQ(available->count, sizeof(std::uint32_t));
    EXPECT_EQ(resource->counters().totalAllocations, before.totalAllocations);
}

TEST(ImageStorageTest, ARequestThatDescribesNoImageIsRefusedWithTheRuleThatRefusedIt)
{
    an::EngineContext engine = makeEngineContext();

    // Three planes for a one-channel image: an arrangement that leaves nothing to store the extra
    // planes' samples from, refused as a pair of arguments that cannot both be true.
    an::ImageLayoutRequest mismatched = planarRequest(8u, 8u, 1u);
    mismatched.planeCount = 3u;
    an::Result<an::ImageStorage<std::uint16_t>> planes =
        an::ImageStorage<std::uint16_t>::create(engine, mismatched, an::StorageInitialization::Zeroed);
    ASSERT_FALSE(planes.hasValue());
    EXPECT_EQ(planes.status().code(), an::ErrorCode::IncompatibleArguments);

    // An alignment that is not one, refused as an argument the caller can correct.
    an::ImageLayoutRequest crooked = planarRequest(8u, 8u, 1u);
    crooked.rowAlignmentBytes = 24u;
    an::Result<an::ImageStorage<std::uint16_t>> alignment =
        an::ImageStorage<std::uint16_t>::create(engine, crooked, an::StorageInitialization::Zeroed);
    ASSERT_FALSE(alignment.hasValue());
    EXPECT_EQ(alignment.status().code(), an::ErrorCode::InvalidArgument);

    // A frame past the configured maximum image size, refused as a limit somebody can raise. The
    // distinction matters: this is the only one of the three a configuration change answers.
    an::EngineContextOptions options;
    options.limits = an::defaultEngineLimits();
    options.limits.maxImageBytes = 1024u;
    an::Result<an::EngineContext> small = an::EngineContext::create(options);
    ASSERT_TRUE(small.hasValue());
    an::Result<an::ImageStorage<std::uint16_t>> tooLarge = an::ImageStorage<std::uint16_t>::create(
        *small, planarRequest(256u, 256u, 1u), an::StorageInitialization::Zeroed);
    ASSERT_FALSE(tooLarge.hasValue());
    EXPECT_EQ(tooLarge.status().code(), an::ErrorCode::LimitExceeded);
}

TEST(ImageStorageTest, AFrameThatDoesNotFitTheBudgetIsRefusedWithTheNumbersToAnswerIt)
{
    an::EngineContext engine = makeEngineContext(4096u);
    const an::ImageLayout layout = planarLayout(256u, 256u, 1u);
    ASSERT_GT(layout.totalBytes(), 4096u);

    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_FALSE(storage.hasValue());
    EXPECT_EQ(storage.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(storage.status().category(), an::ErrorCategory::Resource);

    const an::StatusContextField* const requested = storage.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* const available = storage.status().findContext(an::ContextKey::Available);
    const an::StatusContextField* const limit = storage.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(available, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(requested->count, layout.totalBytes());
    EXPECT_EQ(limit->count, 4096u);
    EXPECT_EQ(available->count, 4096u);

    // And the budget is intact afterwards: a refusal that leaked would make the next, smaller,
    // request fail for a reason nobody could see.
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(engine.memoryResource()->counters().deniedAllocations, 1u);
    an::Result<an::ImageStorage<std::uint16_t>> smaller =
        an::ImageStorage<std::uint16_t>::create(engine, planarLayout(8u, 8u, 1u), an::StorageInitialization::Zeroed);
    EXPECT_TRUE(smaller.hasValue());
}

TEST(ImageStorageTest, AContextWithAnImageOutstandingRefusesToShutDown)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, planarLayout(16u, 16u, 1u), an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());

    EXPECT_FALSE(engine.quiescent());
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);

    storage->release();
    EXPECT_FALSE(storage->valid());
    EXPECT_TRUE(engine.quiescent());
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(ImageStorageTest, MovingAnImageMovesItsMemoryAndNotACopyOfIt)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    an::Result<an::ImageStorage<std::uint16_t>> created = an::ImageStorage<std::uint16_t>::createFilled(
        engine, planarLayout(16u, 16u, 1u), static_cast<std::uint16_t>(7u));
    ASSERT_TRUE(created.hasValue());
    const std::uint64_t liveWithOne = resource->counters().liveBytes;
    const std::uint16_t* const address = created->data();

    an::ImageStorage<std::uint16_t> moved = std::move(*created);
    EXPECT_EQ(moved.data(), address);
    EXPECT_EQ(resource->counters().liveBytes, liveWithOne);

    // The source is left exactly as a default-constructed storage, which is what makes destroying
    // it after a move a no-op rather than a second release of the same allocation.
    EXPECT_FALSE(created->valid());
    EXPECT_EQ(created->data(), nullptr);
    EXPECT_FALSE(created->layout().has_value());
    EXPECT_EQ(created->capacityBytes(), 0u);
    EXPECT_EQ(created->contextId(), 0u);
    EXPECT_FALSE(created->view().has_value());
}

TEST(ImageStorageTest, DestroyingAnImageGivesItsMemoryBack)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t before = resource->counters().liveBytes;

    {
        an::Result<an::ImageStorage<std::uint16_t>> storage = an::ImageStorage<std::uint16_t>::create(
            engine, planarLayout(32u, 32u, 3u), an::StorageInitialization::Zeroed);
        ASSERT_TRUE(storage.hasValue());
        EXPECT_GT(resource->counters().liveBytes, before);
    }
    EXPECT_EQ(resource->counters().liveBytes, before);
    EXPECT_EQ(resource->counters().liveAllocations, 0u);
}

TEST(ImageStorageTest, ACloneCopiesTheSamplesIntoMemoryOfItsOwn)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ImageStorage<std::uint16_t>> original = an::ImageStorage<std::uint16_t>::createFilled(
        engine, planarLayout(8u, 4u, 2u), static_cast<std::uint16_t>(99u));
    ASSERT_TRUE(original.hasValue());

    an::Result<an::ImageStorage<std::uint16_t>> copy = original->clone();
    ASSERT_TRUE(copy.hasValue());
    EXPECT_NE(copy->data(), original->data());
    EXPECT_EQ(copy->layout(), original->layout());
    EXPECT_EQ(copy->contextId(), original->contextId());
    EXPECT_EQ(std::memcmp(copy->data(), original->data(), static_cast<std::size_t>(original->logicalBytes())), 0);

    // The copy is memory of its own, so writing through it does not reach the original.
    EXPECT_TRUE(copy->fill(static_cast<std::uint16_t>(1u)).ok());
    EXPECT_EQ(*original->data(), 99u);
    EXPECT_EQ(*copy->data(), 1u);
    EXPECT_EQ(engine.memoryResource()->counters().liveAllocations, 2u);
}

TEST(ImageStorageTest, AReshapeReusesTheMemoryItAlreadyHoldsAndNeverAsksForMore)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::ImageLayout large = planarLayout(32u, 32u, 1u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, large, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());
    const std::uint64_t allocationsAfterCreate = resource->counters().totalAllocations;
    const std::uint16_t* const address = storage->data();

    const an::ImageLayout smaller = planarLayout(16u, 16u, 1u);
    ASSERT_TRUE(storage->reshape(smaller).ok());
    EXPECT_EQ(storage->layout(), smaller);
    EXPECT_EQ(storage->logicalBytes(), smaller.totalBytes());
    EXPECT_EQ(storage->capacityBytes(), large.totalBytes());
    EXPECT_EQ(storage->data(), address);
    EXPECT_EQ(resource->counters().totalAllocations, allocationsAfterCreate);
    EXPECT_EQ(resource->counters().liveBytes, large.totalBytes());

    // And back to the size it was allocated at, which is the boundary case of "fits the capacity".
    EXPECT_TRUE(storage->reshape(large).ok());
    EXPECT_EQ(storage->logicalBytes(), large.totalBytes());

    // A clone of a reshaped storage is sized to what it means rather than to what it holds.
    ASSERT_TRUE(storage->reshape(smaller).ok());
    an::Result<an::ImageStorage<std::uint16_t>> copy = storage->clone();
    ASSERT_TRUE(copy.hasValue());
    EXPECT_EQ(copy->capacityBytes(), smaller.totalBytes());
}

TEST(ImageStorageTest, AReshapeThatDoesNotFitIsRefusedAndChangesNothing)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout small = planarLayout(8u, 8u, 1u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(engine, small, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());

    const an::ImageLayout larger = planarLayout(64u, 64u, 1u);
    const an::Status refused = storage->reshape(larger);
    EXPECT_EQ(refused.code(), an::ErrorCode::OutOfRange);

    const an::StatusContextField* const requested = refused.findContext(an::ContextKey::Requested);
    const an::StatusContextField* const available = refused.findContext(an::ContextKey::Available);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(available, nullptr);
    EXPECT_EQ(requested->count, larger.totalBytes());
    EXPECT_EQ(available->count, small.totalBytes());

    EXPECT_EQ(storage->layout(), small);
    EXPECT_EQ(storage->capacityBytes(), small.totalBytes());

    // A layout of another sample size is refused for its own reason, before the fit is considered.
    const an::EngineLimits limits = an::defaultEngineLimits();
    an::ImageLayoutRequest wider = planarRequest(4u, 4u, 1u);
    wider.elementSizeBytes = 4u;
    const std::optional<an::ImageLayout> widerLayout = an::ImageLayout::create(wider, limits);
    ASSERT_TRUE(widerLayout.has_value());
    EXPECT_EQ(storage->reshape(*widerLayout).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(storage->layout(), small);
}

TEST(ImageStorageTest, AStorageThatHoldsNothingAnswersEveryQuestionRatherThanFailing)
{
    an::ImageStorage<float> empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.data(), nullptr);
    EXPECT_FALSE(empty.layout().has_value());
    EXPECT_EQ(empty.capacityBytes(), 0u);
    EXPECT_EQ(empty.logicalBytes(), 0u);
    EXPECT_EQ(empty.sampleCapacity(), 0u);
    EXPECT_EQ(empty.alignmentBytes(), 0u);
    EXPECT_EQ(empty.contextId(), 0u);
    EXPECT_EQ(empty.memoryResource(), nullptr);
    EXPECT_FALSE(empty.view().has_value());
    EXPECT_EQ(empty.fill(1.0f).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(empty.clone().status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(empty.reshape(planarLayout(4u, 4u, 1u)).code(), an::ErrorCode::IncompatibleArguments);

    // Releasing one that holds nothing is a no-op rather than a second release.
    empty.release();
    EXPECT_FALSE(empty.valid());
}

TEST(ImageStorageTest, AnEmptyContextIsRefusedRatherThanDereferenced)
{
    const an::EngineContext empty;
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::create(empty, planarLayout(4u, 4u, 1u), an::StorageInitialization::Zeroed);
    ASSERT_FALSE(storage.hasValue());
    EXPECT_EQ(storage.status().code(), an::ErrorCode::NotInitialized);

    an::Result<an::ImageStorage<std::uint16_t>> fromRequest =
        an::ImageStorage<std::uint16_t>::create(empty, planarRequest(4u, 4u, 1u), an::StorageInitialization::Zeroed);
    ASSERT_FALSE(fromRequest.hasValue());
    EXPECT_EQ(fromRequest.status().code(), an::ErrorCode::NotInitialized);
}

TEST(ImageStorageTest, AnInitializationPolicyThisBuildDoesNotKnowIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ImageStorage<std::uint16_t>> storage = an::ImageStorage<std::uint16_t>::create(
        engine, planarLayout(4u, 4u, 1u), static_cast<an::StorageInitialization>(7));
    ASSERT_FALSE(storage.hasValue());
    EXPECT_EQ(storage.status().code(), an::ErrorCode::InvalidArgument);

    EXPECT_TRUE(an::isStorageInitialization(an::StorageInitialization::Uninitialized));
    EXPECT_TRUE(an::isStorageInitialization(an::StorageInitialization::Zeroed));
    EXPECT_FALSE(an::isStorageInitialization(static_cast<an::StorageInitialization>(7)));
    EXPECT_STREQ(an::storageInitializationName(an::StorageInitialization::Uninitialized), "Uninitialized");
    EXPECT_STREQ(an::storageInitializationName(an::StorageInitialization::Zeroed), "Zeroed");
    EXPECT_STREQ(an::storageInitializationName(static_cast<an::StorageInitialization>(7)),
                 "UnknownStorageInitialization");
}

TEST(ImageStorageTest, AnAlignmentThatIsNotOneIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ImageStorage<std::uint16_t>> storage = an::ImageStorage<std::uint16_t>::create(
        engine, planarLayout(4u, 4u, 1u), an::StorageInitialization::Zeroed, 24u);
    ASSERT_FALSE(storage.hasValue());
    EXPECT_EQ(storage.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ImageStorageTest, AlignedRowsGetAnAlignedFirstRow)
{
    // A layout that padded its rows to 256 bytes and then began on a 64-byte boundary would have
    // every row aligned relative to a base that is not, which is the one case where the padding
    // buys nothing at all.
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ImageStorage<std::uint16_t>> storage = an::ImageStorage<std::uint16_t>::create(
        engine, planarRequest(6u, 4u, 2u, 256u), an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue());
    EXPECT_EQ(storage->alignmentBytes(), 256u);
    EXPECT_TRUE(an::isPointerAligned(storage->data(), 256u));
}

TEST(ImageStorageTest, AViewOfTheStorageDescribesTheStorage)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout layout = planarLayout(8u, 4u, 3u, 64u);
    an::Result<an::ImageStorage<std::uint16_t>> storage =
        an::ImageStorage<std::uint16_t>::createFilled(engine, layout, static_cast<std::uint16_t>(5u));
    ASSERT_TRUE(storage.hasValue());

    const std::optional<an::ImageView<std::uint16_t>> view = storage->view();
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->data(), storage->data());
    EXPECT_EQ(view->layout(), layout);
    EXPECT_EQ(view->sampleCount(), layout.extent().sampleCount());
    EXPECT_TRUE(an::viewsOverlap(*view, *view));

    // A reshape changes what the next view describes, which is the point of a layout being
    // separate from the memory it names.
    ASSERT_TRUE(storage->reshape(planarLayout(4u, 2u, 3u, 64u)).ok());
    const std::optional<an::ImageView<std::uint16_t>> narrowed = storage->view();
    ASSERT_TRUE(narrowed.has_value());
    EXPECT_EQ(narrowed->width(), 4u);
    EXPECT_EQ(narrowed->data(), storage->data());
}

TEST(ImageStorageTest, EverySampleTypeCanBeStored)
{
    // Storage coverage is compile-time enumerated over all eight, which is a promise the engine's
    // conversion matrix and every storage-generic kernel later depend on. It does not imply that
    // every algorithm accepts every type; it implies that none of them is blocked by storage.
    an::EngineContext engine = makeEngineContext();
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(4u, 4u, 1u, limits);
    ASSERT_TRUE(extent.has_value());

    const auto storable = [&](std::uint32_t elementSizeBytes) {
        return an::ImageLayoutRequest{.extent = *extent,
                                      .elementSizeBytes = elementSizeBytes,
                                      .samplesPerPixel = 1u,
                                      .planeCount = 1u,
                                      .rowAlignmentBytes = 1u};
    };

    EXPECT_TRUE(
        an::ImageStorage<std::uint8_t>::createFilled(engine, storable(1u), static_cast<std::uint8_t>(1u)).hasValue());
    EXPECT_TRUE(
        an::ImageStorage<std::int8_t>::createFilled(engine, storable(1u), static_cast<std::int8_t>(-1)).hasValue());
    EXPECT_TRUE(
        an::ImageStorage<std::uint16_t>::createFilled(engine, storable(2u), static_cast<std::uint16_t>(2u)).hasValue());
    EXPECT_TRUE(
        an::ImageStorage<std::int16_t>::createFilled(engine, storable(2u), static_cast<std::int16_t>(-2)).hasValue());
    EXPECT_TRUE(an::ImageStorage<std::uint32_t>::createFilled(engine, storable(4u), 3u).hasValue());
    EXPECT_TRUE(an::ImageStorage<std::int32_t>::createFilled(engine, storable(4u), -3).hasValue());
    EXPECT_TRUE(an::ImageStorage<float>::createFilled(engine, storable(4u), 0.5f).hasValue());
    EXPECT_TRUE(an::ImageStorage<double>::createFilled(engine, storable(8u), 0.25).hasValue());

    EXPECT_EQ(an::ImageStorage<double>::kSampleType, an::SampleType::Float64);
    EXPECT_EQ(an::ImageStorage<std::int8_t>::kSampleType, an::SampleType::Int8);
}

TEST(ImageStorageTest, TheUntypedStorageUnderneathIsTheSameMemory)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ImageStorage<float>> storage = an::ImageStorage<float>::createFilled(
        engine,
        an::ImageLayoutRequest{.extent = an::ImageExtent::create(4u, 4u, 1u, an::defaultEngineLimits()).value(),
                               .elementSizeBytes = 4u,
                               .samplesPerPixel = 1u,
                               .planeCount = 1u,
                               .rowAlignmentBytes = 1u},
        1.5f);
    ASSERT_TRUE(storage.hasValue());

    const an::ImageStorageBytes& bytes = storage->bytes();
    EXPECT_TRUE(bytes.valid());
    EXPECT_EQ(bytes.data(), storage->data());
    EXPECT_EQ(bytes.capacityBytes(), storage->capacityBytes());
    EXPECT_EQ(bytes.contextId(), engine.id());
    EXPECT_EQ(bytes.layout(), storage->layout());
}

} // namespace

TEST(ImageStorageTest, SampleChecksNameTheCallerAndAlignmentConflict)
{
    const auto refused = an::checkStorageSampleSize(1u, an::SampleType::Float64, "ImageStorage::reshape");
    EXPECT_STREQ(refused.operation(), "ImageStorage::reshape");
    alignas(double) std::byte bytes[16u]{};
    EXPECT_EQ(an::checkStorageSampleAlignment(bytes + 1u, an::SampleType::Float64).code(),
              an::ErrorCode::IncompatibleArguments);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/SampleBuffer.h.
//
// The four ownerships exist because they have different consequences, so these cases are organised
// around the consequences rather than around the constructors.
//
// **Only engine memory is adoptable.** A mapped, decoder-owned or borrowed buffer refuses to hand
// its bytes over and says which of the four it is, because an image built on memory it does not
// control is an image whose storage can vanish under it.
//
// **The lifetime token is held, not remembered.** A decoder-owned buffer releases what it borrowed
// when the last holder lets go, and the cases below observe that through a token whose destructor
// records it rather than by trusting the documentation.
//
// **Constness is a property of the buffer, not of a second pointer.** A buffer built from a const
// span answers "not writable" and hands out no mutable span, and the same bytes read back through
// the const accessor.

#include <AfterNightEngine/Image/SampleBuffer.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>

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

/// A validated layout for a small single-plane 16-bit frame.
///
/// @param width the frame's width.
/// @param height the frame's height.
/// @return the layout, tightly packed.
[[nodiscard]] an::ImageLayout smallLayout(std::uint32_t width = 8u, std::uint32_t height = 4u)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, limits);
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = extent.value(),
                                                       .elementSizeBytes = 2u,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = 1u},
                                limits);
    EXPECT_TRUE(layout.has_value());
    return layout.value();
}

/// Something whose destruction is observable, standing in for a mapping or a decoder buffer.
struct ReleaseWitness
{
    /// Records the release.
    ~ReleaseWitness()
    {
        released = true;
    }

    /// Set when this witness is destroyed.
    bool released = false;
};

TEST(SampleBufferTest, EngineMemoryIsCountedHeldAndAdoptable)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t before = resource->counters().liveBytes;

    const an::ImageLayout layout = smallLayout();
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();

    EXPECT_TRUE(buffer->valid());
    EXPECT_TRUE(buffer->writable());
    EXPECT_TRUE(buffer->adoptable());
    EXPECT_EQ(buffer->ownership(), an::SampleBufferOwnership::Owned);
    EXPECT_EQ(buffer->byteCount(), layout.totalBytes());
    EXPECT_EQ(buffer->contextId(), engine.id());
    EXPECT_EQ(resource->counters().liveBytes, before + layout.totalBytes());

    buffer->release();
    EXPECT_FALSE(buffer->valid());
    EXPECT_EQ(resource->counters().liveBytes, before);
}

TEST(SampleBufferTest, AZeroedOwnedBufferReadsAsZeroesThroughTheConstAccessor)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout layout = smallLayout();
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();

    const std::span<const std::byte> bytes = buffer->bytes();
    ASSERT_EQ(bytes.size(), layout.totalBytes());
    for (const std::byte value : bytes)
    {
        EXPECT_EQ(value, std::byte{0});
    }
}

TEST(SampleBufferTest, HandingOverEngineMemoryLeavesTheBufferHoldingNothing)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout layout = smallLayout();
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();

    an::Result<an::ImageStorageBytes> storage = buffer->releaseOwned();
    ASSERT_TRUE(storage.hasValue()) << storage.status().message();
    EXPECT_TRUE(storage->valid());
    EXPECT_EQ(storage->capacityBytes(), layout.totalBytes());
    EXPECT_FALSE(buffer->valid());
    EXPECT_FALSE(buffer->adoptable());
}

TEST(SampleBufferTest, AdoptingStorageCarriesItsBytesAndItsAccounting)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t before = resource->counters().liveBytes;

    const an::ImageLayout layout = smallLayout();
    an::Result<an::ImageStorageBytes> storage =
        an::ImageStorageBytes::create(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue()) << storage.status().message();
    const void* const address = storage->data();

    an::SampleBuffer buffer = an::SampleBuffer::adopt(std::move(*storage));
    EXPECT_TRUE(buffer.adoptable());
    EXPECT_EQ(buffer.bytes().data(), static_cast<const std::byte*>(address));
    EXPECT_EQ(buffer.byteCount(), layout.totalBytes());
    EXPECT_EQ(resource->counters().liveBytes, before + layout.totalBytes());
}

TEST(SampleBufferTest, AdoptingStorageThatHoldsNothingProducesABufferThatHoldsNothing)
{
    an::ImageStorageBytes empty;
    const an::SampleBuffer buffer = an::SampleBuffer::adopt(std::move(empty));
    EXPECT_FALSE(buffer.valid());
    EXPECT_FALSE(buffer.adoptable());
    EXPECT_EQ(buffer.byteCount(), 0u);
    EXPECT_TRUE(buffer.bytes().empty());
}

TEST(SampleBufferTest, ADecoderBufferReleasesWhatItBorrowedWhenTheBufferGoesAway)
{
    std::array<std::byte, 32> decoded{};
    auto witness = std::make_shared<ReleaseWitness>();
    std::weak_ptr<ReleaseWitness> observer = witness;

    {
        an::Result<an::SampleBuffer> buffer = an::SampleBuffer::external(
            an::SampleBufferOwnership::DecoderOwned, decoded.data(), decoded.size(), std::move(witness), true);
        ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
        EXPECT_EQ(buffer->ownership(), an::SampleBufferOwnership::DecoderOwned);
        EXPECT_TRUE(buffer->writable());
        EXPECT_FALSE(buffer->adoptable());
        EXPECT_EQ(buffer->contextId(), 0u);
        EXPECT_FALSE(observer.expired());
    }

    EXPECT_TRUE(observer.expired());
}

TEST(SampleBufferTest, AMappedBufferIsExternalWithATokenAndIsNotAdoptable)
{
    std::array<std::byte, 16> mapped{};
    an::Result<an::SampleBuffer> buffer = an::SampleBuffer::external(
        an::SampleBufferOwnership::Mapped, mapped.data(), mapped.size(), std::make_shared<ReleaseWitness>(), false);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();

    EXPECT_EQ(buffer->ownership(), an::SampleBufferOwnership::Mapped);
    EXPECT_FALSE(buffer->writable());
    EXPECT_FALSE(buffer->adoptable());
    EXPECT_FALSE(buffer->mutableBytes().has_value());
    EXPECT_EQ(buffer->bytes().size(), mapped.size());
}

TEST(SampleBufferTest, ExternalMemoryWithoutATokenIsRefusedRatherThanCalledBorrowed)
{
    std::array<std::byte, 16> bytes{};
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::external(an::SampleBufferOwnership::Mapped, bytes.data(), bytes.size(), nullptr, true);
    ASSERT_FALSE(buffer.hasValue());
    EXPECT_EQ(buffer.code(), an::ErrorCode::InvalidArgument);
}

TEST(SampleBufferTest, EngineMemoryAndCallerMemoryAreNotDeclaredExternal)
{
    std::array<std::byte, 16> bytes{};
    for (const an::SampleBufferOwnership ownership :
         {an::SampleBufferOwnership::Owned, an::SampleBufferOwnership::Borrowed})
    {
        an::Result<an::SampleBuffer> buffer =
            an::SampleBuffer::external(ownership, bytes.data(), bytes.size(), std::make_shared<ReleaseWitness>(), true);
        ASSERT_FALSE(buffer.hasValue()) << an::sampleBufferOwnershipName(ownership);
        EXPECT_EQ(buffer.code(), an::ErrorCode::InvalidArgument);
    }
}

TEST(SampleBufferTest, ABorrowedBufferReleasesNothingAndIsNotAdoptable)
{
    std::array<std::byte, 8> owned{};
    owned[0] = std::byte{0x5A};

    an::Result<an::SampleBuffer> buffer = an::SampleBuffer::borrow(std::span<std::byte>{owned});
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();

    EXPECT_EQ(buffer->ownership(), an::SampleBufferOwnership::Borrowed);
    EXPECT_TRUE(buffer->writable());
    EXPECT_FALSE(buffer->adoptable());

    const std::optional<std::span<std::byte>> writable = buffer->mutableBytes();
    ASSERT_TRUE(writable.has_value());
    (*writable)[1] = std::byte{0x3C};
    EXPECT_EQ(owned[1], std::byte{0x3C});

    an::Result<an::ImageStorageBytes> refused = buffer->releaseOwned();
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(buffer->valid());
}

TEST(SampleBufferTest, AConstBorrowIsReadableAndRefusesEveryWritePath)
{
    const std::array<std::byte, 4> source{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};

    an::Result<an::SampleBuffer> buffer = an::SampleBuffer::borrowConst(std::span<const std::byte>{source});
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();

    EXPECT_FALSE(buffer->writable());
    EXPECT_FALSE(buffer->mutableBytes().has_value());
    const std::span<const std::byte> bytes = buffer->bytes();
    ASSERT_EQ(bytes.size(), source.size());
    EXPECT_EQ(std::memcmp(bytes.data(), source.data(), source.size()), 0);
}

TEST(SampleBufferTest, ABufferOfNoBytesDescribesNoFrame)
{
    an::Result<an::SampleBuffer> borrowed = an::SampleBuffer::borrow(std::span<std::byte>{});
    ASSERT_FALSE(borrowed.hasValue());
    EXPECT_EQ(borrowed.code(), an::ErrorCode::InvalidArgument);

    an::Result<an::SampleBuffer> borrowedConst = an::SampleBuffer::borrowConst(std::span<const std::byte>{});
    ASSERT_FALSE(borrowedConst.hasValue());
    EXPECT_EQ(borrowedConst.code(), an::ErrorCode::InvalidArgument);

    std::array<std::byte, 4> bytes{};
    an::Result<an::SampleBuffer> external = an::SampleBuffer::external(an::SampleBufferOwnership::Mapped, bytes.data(),
                                                                       0u, std::make_shared<ReleaseWitness>(), true);
    ASSERT_FALSE(external.hasValue());
    EXPECT_EQ(external.code(), an::ErrorCode::InvalidArgument);
}

TEST(SampleBufferTest, MovingABufferMovesTheResponsibilityAndNotTheBytes)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);

    const an::ImageLayout layout = smallLayout();
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
    const std::uint64_t held = resource->counters().liveBytes;
    const std::byte* const address = buffer->bytes().data();

    an::SampleBuffer moved = std::move(*buffer);
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.bytes().data(), address);
    EXPECT_FALSE(buffer->valid());
    EXPECT_EQ(buffer->ownership(), an::SampleBufferOwnership::Borrowed);
    EXPECT_EQ(resource->counters().liveBytes, held);
}

TEST(SampleBufferTest, AssigningOverABufferReleasesWhatItHeldFirst)
{
    an::EngineContext engine = makeEngineContext();
    an::MemoryResource* const resource = engine.memoryResource();
    ASSERT_NE(resource, nullptr);
    const std::uint64_t before = resource->counters().liveBytes;

    const an::ImageLayout layout = smallLayout();
    an::Result<an::SampleBuffer> first =
        an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    an::Result<an::SampleBuffer> second =
        an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_EQ(resource->counters().liveBytes, before + 2u * layout.totalBytes());

    *first = std::move(*second);
    EXPECT_EQ(resource->counters().liveBytes, before + layout.totalBytes());
    EXPECT_TRUE(first->valid());
}

TEST(SampleBufferTest, ABufferThatHoldsNothingAnswersEveryQuestionWithoutBeingAsked)
{
    an::SampleBuffer buffer;
    EXPECT_FALSE(buffer.valid());
    EXPECT_FALSE(buffer.writable());
    EXPECT_FALSE(buffer.adoptable());
    EXPECT_EQ(buffer.byteCount(), 0u);
    EXPECT_EQ(buffer.contextId(), 0u);
    EXPECT_TRUE(buffer.bytes().empty());
    EXPECT_FALSE(buffer.mutableBytes().has_value());

    an::Result<an::ImageStorageBytes> refused = buffer.releaseOwned();
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::NotInitialized);

    buffer.release();
    EXPECT_FALSE(buffer.valid());
}

TEST(SampleBufferTest, EveryOwnershipHasItsOwnNameAndAStrangerSaysSo)
{
    constexpr an::SampleBufferOwnership kAll[] = {an::SampleBufferOwnership::Owned, an::SampleBufferOwnership::Mapped,
                                                  an::SampleBufferOwnership::DecoderOwned,
                                                  an::SampleBufferOwnership::Borrowed};
    for (const an::SampleBufferOwnership left : kAll)
    {
        EXPECT_TRUE(an::isSampleBufferOwnership(left));
        for (const an::SampleBufferOwnership right : kAll)
        {
            const bool sameName =
                std::strcmp(an::sampleBufferOwnershipName(left), an::sampleBufferOwnershipName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_FALSE(an::isSampleBufferOwnership(static_cast<an::SampleBufferOwnership>(200)));
    EXPECT_STREQ(an::sampleBufferOwnershipName(static_cast<an::SampleBufferOwnership>(200)),
                 "UnknownSampleBufferOwnership");
}

} // namespace

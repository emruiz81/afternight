// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The large blocks a session keeps while a preview dialog is open: a block given back serves the next request of its
/// size, cleared for a zeroed one, two images of one size alternate between the same two blocks, the working buffers
/// of every worker are kept while a large size keeps three, nothing is kept without a reservation or once the last one
/// goes, and storage that outlives its session gives its memory back.

#include "ConsumerRuntimeTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <thread>
#include <optional>
#include <utility>
#include <vector>

namespace
{

using an::tests::consumer::filledImage;
using an::tests::consumer::monoFloatDescriptor;
using an::tests::consumer::openSession;

/// Two kibibytes of Float32 samples a row.
constexpr std::uint32_t kWidth = 512u;

/// Rows of an image four mebibytes large, of one of another large size, of a working buffer of half a mebibyte and of
/// a block of sixteen mebibytes.
constexpr std::uint32_t kRows = 2048u;
constexpr std::uint32_t kOtherRows = 1536u;
constexpr std::uint32_t kWorkingRows = 256u;
constexpr std::uint32_t kLargeRows = 8192u;

/// The first sample's address, which is where an image's block begins.
[[nodiscard]] const void* blockOf(const an::Image<float>& image)
{
    const auto view = image.view();
    return view.has_value() ? static_cast<const void*>(view->data()) : nullptr;
}

TEST(RecycledMemoryTest, WhileAPreviewIsReservedALargeBlockGivenBackServesTheNextRequestOfItsSize)
{
    std::optional<an::consumer::EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::SessionHold> hold = session->hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();

    {
        an::Result<an::Image<float>> before = filledImage(hold->engine(), kWidth, kRows, 1.0f);
        ASSERT_TRUE(before.hasValue()) << before.status().message();
    }
    EXPECT_EQ(session->recycledMemory().keptBlocks, 0u) << "nothing is kept while no preview is reserved";

    an::Result<an::consumer::PreviewReservation> reservation = session->reservePreview();
    ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();
    const void* first = nullptr;
    {
        an::Result<an::Image<float>> image = filledImage(hold->engine(), kWidth, kRows, 2.0f);
        ASSERT_TRUE(image.hasValue()) << image.status().message();
        first = blockOf(*image);
    }
    const an::consumer::RecycledMemory kept = session->recycledMemory();
    EXPECT_EQ(kept.keptBlocks, 1u);
    EXPECT_GE(kept.keptBytes, static_cast<std::uint64_t>(kWidth) * kRows * sizeof(float));

    an::Result<an::Image<float>> again = filledImage(hold->engine(), kWidth, kRows, 3.0f);
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_EQ(blockOf(*again), first) << "the next request of the size took the kept block";
    EXPECT_EQ(session->recycledMemory().reusedBlocks, 1u);
    EXPECT_EQ(session->recycledMemory().keptBlocks, 0u);
    EXPECT_EQ(an::tests::consumer::sampleAt(an::consumer::AnyImage(std::move(*again)), 7u, 9u), 3.0f);

    // Two images of one size, each given back when the next is made, alternate between the same two blocks.
    std::optional<an::Image<float>> shown;
    std::vector<const void*> blocks;
    for (int frame = 0; frame < 6; ++frame)
    {
        an::Result<an::Image<float>> next = filledImage(hold->engine(), kWidth, kRows, static_cast<float>(frame));
        ASSERT_TRUE(next.hasValue()) << next.status().message();
        blocks.push_back(blockOf(*next));
        shown = std::move(*next);
    }
    shown.reset();
    for (std::size_t frame = 2u; frame < blocks.size(); ++frame)
    {
        EXPECT_EQ(blocks[frame], blocks[frame - 2u]) << "frame " << frame;
        EXPECT_NE(blocks[frame], blocks[frame - 1u]) << "frame " << frame;
    }

    reservation->release();
    EXPECT_EQ(session->recycledMemory().keptBlocks, 0u) << "the last reservation gone, every kept block went back";
    EXPECT_EQ(session->recycledMemory().keptBytes, 0u);
}

// The blocks a preview dialog's frames left kept go back to the platform on the session's own thread when the last
// dialog closes, whatever the memory budget: the dialog closes on the GUI thread, which under a stated budget freed
// every one of them itself - up to the most a session keeps, each a large free - before the close returned.
TEST(RecycledMemoryTest, BlocksAClosingPreviewLeftKeptGoBackOnTheSessionsOwnThreadUnderABudget)
{
    an::consumer::EngineSessionSettings settings = an::tests::consumer::settingsOfWidth(2u);
    settings.limits.memoryBudgetBytes = std::uint64_t{8} << 30u;
    an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    an::consumer::EngineSession& session = *opened;
    an::Result<an::consumer::SessionHold> hold = session.hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();

    an::Result<an::consumer::PreviewReservation> reservation = session.reservePreview();
    ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();
    {
        an::Result<an::Image<float>> image = filledImage(hold->engine(), kWidth, kLargeRows, 1.0f);
        ASSERT_TRUE(image.hasValue()) << image.status().message();
    }
    ASSERT_EQ(session.recycledMemory().keptBlocks, 1u);
    const std::uint64_t before = session.recycledMemory().releasedLater;

    reservation->release();
    EXPECT_EQ(session.recycledMemory().keptBlocks, 0u);
    EXPECT_EQ(session.recycledMemory().releasedLater, before + 1u)
        << "the closing dialog's thread gave the kept block back itself";
}

TEST(RecycledMemoryTest, BlocksAreKeptUntilTheLastOfSeveralReservationsGoes)
{
    std::optional<an::consumer::EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::SessionHold> hold = session->hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();
    an::Result<an::consumer::PreviewReservation> first = session->reservePreview();
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    an::Result<an::consumer::PreviewReservation> second = session->reservePreview();
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    {
        an::Result<an::Image<float>> image = filledImage(hold->engine(), kWidth, kRows, 1.0f);
        ASSERT_TRUE(image.hasValue()) << image.status().message();
    }
    ASSERT_EQ(session->recycledMemory().keptBlocks, 1u);

    first->release();
    EXPECT_EQ(session->recycledMemory().keptBlocks, 1u) << "a dialog still open keeps what its frames gave back";
    {
        an::Result<an::Image<float>> image = filledImage(hold->engine(), kWidth, kRows, 2.0f);
        ASSERT_TRUE(image.hasValue()) << image.status().message();
    }
    EXPECT_EQ(session->recycledMemory().reusedBlocks, 1u);
    EXPECT_EQ(session->recycledMemory().keptBlocks, 1u);

    second->release();
    EXPECT_EQ(session->recycledMemory().keptBlocks, 0u) << "the last reservation gone, every kept block went back";
}

TEST(RecycledMemoryTest, ASmallBlockAndAnotherSizeAreNeverHandedAKeptBlockAndAZeroedRequestFindsItCleared)
{
    std::optional<an::consumer::EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::SessionHold> hold = session->hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();
    an::Result<an::consumer::PreviewReservation> reservation = session->reservePreview();
    ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();

    {
        an::Result<an::Image<float>> small = filledImage(hold->engine(), 64u, 64u, 5.0f);
        ASSERT_TRUE(small.hasValue()) << small.status().message();
    }
    EXPECT_EQ(session->recycledMemory().keptBlocks, 0u) << "a block the platform's heap keeps itself is not kept";

    const void* kept = nullptr;
    {
        an::Result<an::Image<float>> large = filledImage(hold->engine(), kWidth, kRows, 7.0f);
        ASSERT_TRUE(large.hasValue()) << large.status().message();
        kept = blockOf(*large);
    }
    ASSERT_EQ(session->recycledMemory().keptBlocks, 1u);

    an::Result<an::Image<float>> other = filledImage(hold->engine(), kWidth, kOtherRows, 9.0f);
    ASSERT_TRUE(other.hasValue()) << other.status().message();
    EXPECT_NE(blockOf(*other), kept) << "a request of another size takes new memory";
    EXPECT_EQ(session->recycledMemory().reusedBlocks, 0u);
    EXPECT_EQ(session->recycledMemory().keptBlocks, 1u);

    const std::optional<an::ImageDescriptor> described = monoFloatDescriptor(hold->engine(), kWidth, kRows);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<float>> zeroed = an::Image<float>::create(
        hold->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(zeroed.hasValue()) << zeroed.status().message();
    EXPECT_EQ(blockOf(*zeroed), kept) << "a zeroed request of the kept size takes the kept block";
    EXPECT_EQ(session->recycledMemory().reusedBlocks, 1u);
    for (std::uint32_t y = 0u; y < kRows; ++y)
    {
        const auto row = zeroed->view()->row(0u, y);
        ASSERT_TRUE(row.has_value());
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            ASSERT_EQ((*row)[x], 0.0f) << "the kept block's samples were not cleared at " << x << " " << y;
        }
    }
}

TEST(RecycledMemoryTest, AWorkingBufferOfEveryWorkerIsKeptWhileALargeSizeKeepsThree)
{
    std::optional<an::consumer::EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::SessionHold> hold = session->hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();
    an::Result<an::consumer::PreviewReservation> reservation = session->reservePreview();
    ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();

    // Forty half-mebibyte buffers, as forty workers each take one, all given back and all asked for again.
    constexpr std::size_t kWorkers = 40u;
    std::vector<an::Image<float>> buffers;
    for (std::size_t worker = 0u; worker < kWorkers; ++worker)
    {
        an::Result<an::Image<float>> buffer = filledImage(hold->engine(), kWidth, kWorkingRows, 1.0f);
        ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
        buffers.push_back(std::move(*buffer));
    }
    buffers.clear();
    EXPECT_EQ(session->recycledMemory().keptBlocks, kWorkers);
    for (std::size_t worker = 0u; worker < kWorkers; ++worker)
    {
        an::Result<an::Image<float>> buffer = filledImage(hold->engine(), kWidth, kWorkingRows, 2.0f);
        ASSERT_TRUE(buffer.hasValue()) << buffer.status().message();
        buffers.push_back(std::move(*buffer));
    }
    EXPECT_EQ(session->recycledMemory().reusedBlocks, kWorkers);
    EXPECT_EQ(session->recycledMemory().keptBlocks, 0u);
    buffers.clear();

    // Five sixteen-mebibyte blocks given back: three of them are kept.
    const std::uint64_t keptBefore = session->recycledMemory().keptBlocks;
    std::vector<an::Image<float>> large;
    for (int block = 0; block < 5; ++block)
    {
        an::Result<an::Image<float>> image = filledImage(hold->engine(), kWidth, kLargeRows, 3.0f);
        ASSERT_TRUE(image.hasValue()) << image.status().message();
        large.push_back(std::move(*image));
    }
    large.clear();
    EXPECT_EQ(session->recycledMemory().keptBlocks, keptBefore + 3u);
}

TEST(RecycledMemoryTest, StorageThatOutlivesItsSessionGivesItsMemoryBack)
{
    std::optional<an::Image<float>> survivor;
    {
        std::optional<an::consumer::EngineSession> session = openSession(2u);
        ASSERT_TRUE(session.has_value());
        an::Result<an::consumer::PreviewReservation> reservation = session->reservePreview();
        ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();
        an::Result<an::consumer::SessionHold> hold = session->hold();
        ASSERT_TRUE(hold.hasValue()) << hold.status().message();
        an::Result<an::Image<float>> image = filledImage(hold->engine(), kWidth, kRows, 4.0f);
        ASSERT_TRUE(image.hasValue()) << image.status().message();
        survivor = std::move(*image);
        {
            an::Result<an::Image<float>> kept = filledImage(hold->engine(), kWidth, kRows, 6.0f);
            ASSERT_TRUE(kept.hasValue()) << kept.status().message();
        }
        EXPECT_EQ(session->recycledMemory().keptBlocks, 1u);
    }
    // The session, its hold and its reservation are gone; the image still holds storage from its context, and the
    // resource that storage came from is still there to take it back.
    ASSERT_TRUE(survivor.has_value());
    EXPECT_EQ(an::tests::consumer::sampleAt(an::consumer::AnyImage(std::move(*survivor)), 3u, 5u), 4.0f);
}

// Blocks given back and taken from several threads at once - the frames' workers, the presentation's, a histogram's -
// are each handed to one request: an image every sample of which a thread wrote reads back as that thread's, and what
// is kept stays within the resource's bounds.
TEST(RecycledMemoryTest, BlocksGivenBackAndTakenFromSeveralThreadsAtOnceAreEachHandedToOneRequest)
{
    std::optional<an::consumer::EngineSession> session = openSession(4u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::SessionHold> hold = session->hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();
    an::Result<an::consumer::PreviewReservation> reservation = session->reservePreview();
    ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();

    constexpr int kThreads = 4;
    constexpr int kFramesEach = 32;
    std::atomic<int> wrongSamples{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t]() {
            for (int frame = 0; frame < kFramesEach; ++frame)
            {
                const float value = static_cast<float>(1000 * t + frame);
                an::Result<an::Image<float>> image = filledImage(hold->engine(), kWidth, kRows, value);
                if (!image.hasValue())
                {
                    ++failures;
                    continue;
                }
                std::this_thread::yield();
                if (an::tests::consumer::sampleAt(an::consumer::AnyImage(std::move(*image)), 7u, 9u) != value)
                {
                    ++wrongSamples;
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(wrongSamples.load(), 0) << "a block was handed to two requests at once";
    const an::consumer::RecycledMemory kept = session->recycledMemory();
    EXPECT_GT(kept.reusedBlocks, 0u) << "blocks given back served later requests";
    EXPECT_LE(kept.keptBlocks, 8u) << "no more blocks of one size are kept than the resource allows";

    reservation->release();
    EXPECT_EQ(session->recycledMemory().keptBlocks, 0u) << "the last reservation gone, every kept block went back";
}

} // namespace

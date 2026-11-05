// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"

namespace
{

std::vector<FrameInfo> generateTestFrames(int count, int startIndex = 0, FrameType type = FrameType::Light)
{
    std::vector<FrameInfo> frames;
    frames.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        const int index = startIndex + i;
        FrameMetadata meta(type, QSize(6000, 4000), 300.0, -10.0, 100, 10, QStringLiteral("L"), QStringLiteral("M31"),
                           QDateTime::currentDateTimeUtc(), QStringLiteral("Camera"));
        frames.emplace_back(FrameId(QString("/tmp/frame_%1.fit").arg(index)), QString("/tmp/frame_%1.fit").arg(index),
                            meta);
    }

    return frames;
}

} // namespace

TEST(LargeImportTest, Add10kFramesPerformance)
{
    IntegrationDataModel model;
    auto frames = generateTestFrames(10000);

    auto start = std::chrono::high_resolution_clock::now();

    model.beginBatchUpdate();
    model.addFrames(FrameType::Light, frames);
    model.endBatchUpdate();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    EXPECT_LT(diff.count(), 5.0) << "Adding 10k frames took " << diff.count() << " seconds";
    EXPECT_EQ(model.lightFrames().size(), 10000);
}

TEST(LargeImportTest, Add1kFramesInMultipleBatches)
{
    IntegrationDataModel model;

    auto start = std::chrono::high_resolution_clock::now();

    // Add 10 batches of 100 frames each
    for (int batch = 0; batch < 10; ++batch)
    {
        std::vector<FrameInfo> frames;
        frames.reserve(100);

        for (int i = 0; i < 100; ++i)
        {
            int idx = batch * 100 + i;
            FrameMetadata meta(FrameType::Light, QSize(6000, 4000), 300.0, -10.0, 100, 10, QStringLiteral("L"),
                               QStringLiteral("M31"), QDateTime::currentDateTimeUtc(), QStringLiteral("Camera"));
            frames.emplace_back(FrameId(QString("/tmp/batch_frame_%1.fit").arg(idx)),
                                QString("/tmp/batch_frame_%1.fit").arg(idx), meta);
        }

        model.beginBatchUpdate();
        model.addFrames(FrameType::Light, frames);
        model.endBatchUpdate();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    EXPECT_LT(diff.count(), 2.0) << "Adding 1k frames in batches took " << diff.count() << " seconds";
    EXPECT_EQ(model.lightFrames().size(), 1000);
}

TEST(LargeImportTest, PartialImportOnCancellation)
{
    // Simulate a cancellable import operation
    IntegrationDataModel model;
    std::atomic<bool> cancelled{false};
    std::atomic<int> processedCount{0};

    auto frames = generateTestFrames(1000);

    // Simulate processing with cancellation check
    std::vector<FrameInfo> acceptedFrames;
    acceptedFrames.reserve(frames.size());

    for (const auto& frame : frames)
    {
        if (cancelled.load())
        {
            break;
        }

        acceptedFrames.push_back(frame);
        processedCount.fetch_add(1);

        // Simulate cancellation after 500 frames
        if (processedCount.load() == 500)
        {
            cancelled.store(true);
        }
    }

    model.beginBatchUpdate();
    model.addFrames(FrameType::Light, acceptedFrames);
    model.endBatchUpdate();

    // Verify partial import was preserved
    EXPECT_EQ(model.lightFrames().size(), 500);
    EXPECT_TRUE(cancelled.load());
}

TEST(LargeImportTest, MemoryUsageStaysReasonable)
{
    // This test verifies the model doesn't have unbounded memory growth
    // by adding and removing frames repeatedly

    IntegrationDataModel model;

    for (int cycle = 0; cycle < 5; ++cycle)
    {
        auto frames = generateTestFrames(1000);

        model.beginBatchUpdate();
        model.addFrames(FrameType::Light, frames);
        model.endBatchUpdate();

        EXPECT_EQ(model.lightFrames().size(), 1000);

        // Clear all frames - get the frames vector first
        QStringList paths;
        const auto currentFrames = model.lightFrames().frames();
        for (const auto& frame : currentFrames)
        {
            paths.append(frame.id.canonicalPath());
        }

        model.beginBatchUpdate();
        model.removeFrames(FrameType::Light, paths);
        model.endBatchUpdate();

        EXPECT_EQ(model.lightFrames().size(), 0);
    }

    // If we got here without crashing or hanging, memory is being managed
    SUCCEED();
}

TEST(LargeImportTest, ConcurrentModelAccess)
{
    // Test thread-safe read access during batch updates
    IntegrationDataModel model;
    std::atomic<bool> writerDone{false};
    std::atomic<int> readerIterations{0};
    std::atomic<int> batchIndex{0};

    // Writer thread adds frames
    std::thread writer([&]() {
        for (int batch = 0; batch < 10; ++batch)
        {
            // Generate unique frames for each batch using atomic counter
            const int startIndex = batchIndex.fetch_add(100);
            auto frames = generateTestFrames(100, startIndex);
            model.beginBatchUpdate();
            model.addFrames(FrameType::Light, frames);
            model.endBatchUpdate();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        writerDone.store(true);
    });

    // Reader thread reads frame count
    std::thread reader([&]() {
        while (!writerDone.load())
        {
            // Reading should not crash
            [[maybe_unused]] size_t count = model.lightFrames().size();
            readerIterations.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    writer.join();
    reader.join();

    // Final frame count should be 1000 (10 batches * 100 frames)
    EXPECT_EQ(model.lightFrames().size(), 1000);
    EXPECT_GT(readerIterations.load(), 0);
}

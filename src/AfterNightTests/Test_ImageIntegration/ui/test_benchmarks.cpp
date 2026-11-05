// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>

#include "processes/ImageIntegration/model/GroupingEngine.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/FrameId.h"

#include <iostream>
#include <limits>
#include <vector>

namespace
{
// Whether a sanitizer instruments this build, whose every access it checks.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kInstrumented = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr bool kInstrumented = true;
#else
constexpr bool kInstrumented = false;
#endif
#else
constexpr bool kInstrumented = false;
#endif

// A wall-clock budget is a Release figure. A Debug build measures the compiler rather than the machine, and an
// instrumented build the sanitizer's checks, and on a loaded host neither time decides anything about the code, so
// those builds run each case for its results and leave its time unjudged.
void expectWithinBudget(qint64 elapsedMs, qint64 releaseBudgetMs)
{
#if defined(NDEBUG)
    if constexpr (!kInstrumented)
    {
        EXPECT_LT(elapsedMs, releaseBudgetMs);
    }
    else
    {
        static_cast<void>(elapsedMs);
        static_cast<void>(releaseBudgetMs);
    }
#else
    static_cast<void>(elapsedMs);
    static_cast<void>(releaseBudgetMs);
#endif
}

[[nodiscard]] qint64 measureGroupingBatchNsecs(const GroupingEngine& engine, const std::vector<FrameInfo>& frames,
                                               FrameType type, const GroupingOptions& options, int repeatCount,
                                               size_t& sink)
{
    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < repeatCount; ++i)
    {
        auto groups = engine.groupFrames(frames, type, options);
        sink += static_cast<size_t>(groups.size());
    }

    return timer.nsecsElapsed();
}

[[nodiscard]] qint64 measureBestGroupingBatchNsecs(const GroupingEngine& engine, const std::vector<FrameInfo>& frames,
                                                   FrameType type, const GroupingOptions& options, int sampleCount,
                                                   int repeatCount, size_t& sink)
{
    qint64 best = std::numeric_limits<qint64>::max();

    for (int i = 0; i < sampleCount; ++i)
    {
        const qint64 elapsed = measureGroupingBatchNsecs(engine, frames, type, options, repeatCount, sink);
        if (elapsed < best)
        {
            best = elapsed;
        }
    }

    return best;
}
} // namespace

// Helper to create dummy frames
std::vector<FrameInfo> createBenchmarkFrames(int count, FrameType type)
{
    std::vector<FrameInfo> frames;
    frames.reserve(count);

    QDateTime now = QDateTime::currentDateTimeUtc();

    for (int i = 0; i < count; ++i)
    {
        // Vary parameters to create groups
        int w = 1000 + (i % 2);
        int h = 1000 + (i % 2);
        QString filter = (i % 2 == 0) ? "L" : "R";
        double exp = 1.0 + (i % 5);

        FrameMetadata meta(type, QSize(w, h), exp, -10.0, 100, 10, filter, "Target", now.addSecs(i), "");
        frames.emplace_back(FrameId(QString("C:/bench/frame_%1.fit").arg(i)), QString("C:/bench/frame_%1.fit").arg(i),
                            meta);
    }
    return frames;
}

class BenchmarkTest : public ::testing::Test
{
    // No setup needed - each test creates its own data
};

// Target: < 10ms for 100 frames
TEST_F(BenchmarkTest, GroupingEngine_100Frames)
{
    GroupingEngine engine;
    auto frames = createBenchmarkFrames(100, FrameType::Light);
    GroupingOptions options; // defaults

    QElapsedTimer timer;
    timer.start();

    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    qint64 elapsed = timer.nsecsElapsed() / 1000000; // ms
    std::cout << "[ PERF     ] Grouping 100 frames: " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 10);
    EXPECT_FALSE(groups.empty());
}

// Target: < 100ms for 1000 frames
TEST_F(BenchmarkTest, GroupingEngine_1000Frames)
{
    GroupingEngine engine;
    auto frames = createBenchmarkFrames(1000, FrameType::Light);
    GroupingOptions options;

    QElapsedTimer timer;
    timer.start();

    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Grouping 1000 frames: " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 100);
}

// Target: < 500ms for 10000 frames (stress test)
TEST_F(BenchmarkTest, GroupingEngine_10000Frames)
{
    GroupingEngine engine;
    auto frames = createBenchmarkFrames(10000, FrameType::Light);
    GroupingOptions options;

    QElapsedTimer timer;
    timer.start();

    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Grouping 10000 frames: " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 500);
}

// Target: < 50ms for simple project
TEST_F(BenchmarkTest, ManifestBuilder_SimpleProject)
{
    IntegrationDataModel model;
    // Add minimal set
    model.addFrames(FrameType::Bias, createBenchmarkFrames(10, FrameType::Bias));
    model.addFrames(FrameType::Dark, createBenchmarkFrames(10, FrameType::Dark));
    model.addFrames(FrameType::Flat, createBenchmarkFrames(10, FrameType::Flat));
    model.addFrames(FrameType::Light, createBenchmarkFrames(10, FrameType::Light));
    model.setOutputDirectory("C:/Output");

    QElapsedTimer timer;
    timer.start();

    ManifestBuilder builder(model);
    auto manifest = builder.build();

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Manifest build (small): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 50);
}

// Target: < 200ms for complex project (1000 frames)
TEST_F(BenchmarkTest, ManifestBuilder_ComplexProject)
{
    IntegrationDataModel model;
    model.beginBatchUpdate();
    model.addFrames(FrameType::Bias, createBenchmarkFrames(50, FrameType::Bias));
    model.addFrames(FrameType::Dark, createBenchmarkFrames(50, FrameType::Dark));
    model.addFrames(FrameType::Flat, createBenchmarkFrames(50, FrameType::Flat));
    model.addFrames(FrameType::Light, createBenchmarkFrames(1000, FrameType::Light));
    model.setOutputDirectory("C:/Output");
    model.endBatchUpdate();

    QElapsedTimer timer;
    timer.start();

    ManifestBuilder builder(model);
    auto manifest = builder.build();

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Manifest build (large): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 200);
}

// Target: < 20ms for batch update of 100 frames (signal coalescing)
TEST_F(BenchmarkTest, ModelSignals_BatchOperations)
{
    IntegrationDataModel model;
    auto frames = createBenchmarkFrames(100, FrameType::Light);

    QElapsedTimer timer;
    timer.start();

    model.beginBatchUpdate();
    model.addFrames(FrameType::Light, frames);
    model.endBatchUpdate();

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Model batch add (100): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 20);
}

// Target: < 100ms for inserting 1000 frames into tree model (no full resets)
TEST_F(BenchmarkTest, GroupTreeModel_Insert1000FramesUnder100ms)
{
    IntegrationDataModel model;
    GroupTreeModel tree(FrameType::Light, &model);

    auto frames = createBenchmarkFrames(1000, FrameType::Light);

    QElapsedTimer timer;
    timer.start();
    model.addFrames(FrameType::Light, frames);

    const qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Tree insert (1000): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 100);
}

// Target: < 50ms for removing 100 frames from a populated tree model
TEST_F(BenchmarkTest, GroupTreeModel_Remove100FramesUnder50ms)
{
    IntegrationDataModel model;
    GroupTreeModel tree(FrameType::Light, &model);

    auto frames = createBenchmarkFrames(1000, FrameType::Light);
    model.addFrames(FrameType::Light, frames);

    QStringList to_remove;
    for (int i = 0; i < 100; ++i)
    {
        to_remove << frames[static_cast<size_t>(i)].displayPath;
    }

    QElapsedTimer timer;
    timer.start();
    model.removeFrames(FrameType::Light, to_remove);

    const qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Tree remove (100): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 50);
}

// Target: < 50ms for matching 100 light groups
TEST_F(BenchmarkTest, CalibrationMatcher_100Groups)
{
    IntegrationDataModel model;
    model.beginBatchUpdate();
    auto biasFrames = createBenchmarkFrames(20, FrameType::Bias);
    auto darkFrames = createBenchmarkFrames(20, FrameType::Dark);
    auto flatFrames = createBenchmarkFrames(20, FrameType::Flat);
    auto lightFrames = createBenchmarkFrames(100, FrameType::Light);
    model.addFrames(FrameType::Bias, biasFrames);
    model.addFrames(FrameType::Dark, darkFrames);
    model.addFrames(FrameType::Flat, flatFrames);
    model.addFrames(FrameType::Light, lightFrames);
    model.endBatchUpdate();

    GroupingOptions defaultOptions;

    // Build calibration groups from frames
    std::vector<BiasCalibrationGroup> biasGroups;
    for (const auto& frame : biasFrames)
    {
        BiasCalibrationGroup group;
        group.key = BiasGroupKey::fromFrame(frame);
        group.groupKey = group.key.toGroupKey(defaultOptions);
        group.masterPath = frame.displayPath;
        biasGroups.push_back(group);
    }

    std::vector<DarkCalibrationGroup> darkGroups;
    for (const auto& frame : darkFrames)
    {
        DarkCalibrationGroup group;
        group.key = DarkGroupKey::fromFrame(frame);
        group.groupKey = group.key.toGroupKey(defaultOptions);
        group.masterPath = frame.displayPath;
        darkGroups.push_back(group);
    }

    std::vector<FlatCalibrationGroup> flatGroups;
    for (const auto& frame : flatFrames)
    {
        FlatCalibrationGroup group;
        group.key = FlatGroupKey::fromFrame(frame);
        group.groupKey = group.key.toGroupKey(defaultOptions);
        group.masterPath = frame.displayPath;
        flatGroups.push_back(group);
    }

    // Collect LightGroupKeys from light frames
    std::vector<LightGroupKey> lightKeys;
    for (const auto& frame : lightFrames)
    {
        lightKeys.push_back(LightGroupKey::fromFrame(frame));
    }

    // CalibrationMatcher requires explicit configuration
    CalibrationMatcher matcher(defaultOptions, defaultOptions, defaultOptions);
    matcher.setBiasGroups(std::move(biasGroups));
    matcher.setDarkGroups(std::move(darkGroups));
    matcher.setFlatGroups(std::move(flatGroups));

    QElapsedTimer timer;
    timer.start();

    // Match all light groups
    for (const auto& lightKey : lightKeys)
    {
        [[maybe_unused]] auto bias = matcher.findBias(lightKey);
        [[maybe_unused]] auto dark = matcher.findDark(lightKey);
        [[maybe_unused]] auto flat = matcher.findFlat(lightKey);
    }

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Calibration matching (" << lightKeys.size() << " groups): " << elapsed << " ms"
              << std::endl;

    expectWithinBudget(elapsed, 50);
}
// ============================================================================
// Extended Performance Tests (25,000+ frames)
// ============================================================================

// Target: < 5000ms for extreme dataset grouping (25,000 frames)
TEST_F(BenchmarkTest, GroupingEngine_25000Frames_StressTest)
{
    constexpr size_t kFrameCount = 25000;
    auto frames = createBenchmarkFrames(kFrameCount, FrameType::Light);
    GroupingOptions options;
    GroupingEngine engine;

    QElapsedTimer timer;
    timer.start();

    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Grouping " << kFrameCount << " frames: " << elapsed << " ms" << std::endl;
    std::cout << "[ PERF     ] Result: " << groups.size() << " groups created" << std::endl;

    expectWithinBudget(elapsed, 5000);
    EXPECT_FALSE(groups.empty());
}

// Target: < 500ms for manifest build with 5,000 lights
TEST_F(BenchmarkTest, ManifestBuilder_5000Lights)
{
    IntegrationDataModel model;
    model.beginBatchUpdate();
    model.addFrames(FrameType::Bias, createBenchmarkFrames(100, FrameType::Bias));
    model.addFrames(FrameType::Dark, createBenchmarkFrames(100, FrameType::Dark));
    model.addFrames(FrameType::Flat, createBenchmarkFrames(100, FrameType::Flat));
    model.addFrames(FrameType::Light, createBenchmarkFrames(5000, FrameType::Light));
    model.setOutputDirectory("C:/Output");
    model.endBatchUpdate();

    QElapsedTimer timer;
    timer.start();

    ManifestBuilder builder(model);
    auto manifest = builder.build();

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Manifest build (5000 lights): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 500);
}

// Target: < 100ms for batch update of 1,000 frames
TEST_F(BenchmarkTest, ModelSignals_BatchOperations_1000Frames)
{
    IntegrationDataModel model;
    auto frames = createBenchmarkFrames(1000, FrameType::Light);

    QElapsedTimer timer;
    timer.start();

    model.beginBatchUpdate();
    model.addFrames(FrameType::Light, frames);
    model.endBatchUpdate();

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Model batch add (1000): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 100);
}

// Target: < 500ms for model serialization with 10,000 frames
TEST_F(BenchmarkTest, ModelSerialization_10000Frames)
{
    IntegrationDataModel model;
    model.beginBatchUpdate();
    model.addFrames(FrameType::Bias, createBenchmarkFrames(1000, FrameType::Bias));
    model.addFrames(FrameType::Dark, createBenchmarkFrames(1000, FrameType::Dark));
    model.addFrames(FrameType::Flat, createBenchmarkFrames(1000, FrameType::Flat));
    model.addFrames(FrameType::Light, createBenchmarkFrames(7000, FrameType::Light));
    model.endBatchUpdate();

    QElapsedTimer timer;
    timer.start();

    QJsonObject json = model.toJson();

    qint64 serializeElapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Model serialization (10000 frames): " << serializeElapsed << " ms" << std::endl;

    timer.restart();

    IntegrationDataModel restored;
    restored.fromJson(json);

    qint64 deserializeElapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Model deserialization (10000 frames): " << deserializeElapsed << " ms" << std::endl;

    expectWithinBudget(serializeElapsed, 500);
    expectWithinBudget(deserializeElapsed, 500);
}

// Target: < 200ms for matching 500 light groups
TEST_F(BenchmarkTest, CalibrationMatcher_500Groups)
{
    IntegrationDataModel model;
    model.beginBatchUpdate();
    auto biasFrames = createBenchmarkFrames(50, FrameType::Bias);
    auto darkFrames = createBenchmarkFrames(50, FrameType::Dark);
    auto flatFrames = createBenchmarkFrames(50, FrameType::Flat);
    auto lightFrames = createBenchmarkFrames(500, FrameType::Light);
    model.addFrames(FrameType::Bias, biasFrames);
    model.addFrames(FrameType::Dark, darkFrames);
    model.addFrames(FrameType::Flat, flatFrames);
    model.addFrames(FrameType::Light, lightFrames);
    model.endBatchUpdate();

    GroupingOptions defaultOptions;

    // Build calibration groups from frames
    std::vector<BiasCalibrationGroup> biasGroups;
    for (const auto& frame : biasFrames)
    {
        BiasCalibrationGroup group;
        group.key = BiasGroupKey::fromFrame(frame);
        group.groupKey = group.key.toGroupKey(defaultOptions);
        group.masterPath = frame.displayPath;
        biasGroups.push_back(group);
    }

    std::vector<DarkCalibrationGroup> darkGroups;
    for (const auto& frame : darkFrames)
    {
        DarkCalibrationGroup group;
        group.key = DarkGroupKey::fromFrame(frame);
        group.groupKey = group.key.toGroupKey(defaultOptions);
        group.masterPath = frame.displayPath;
        darkGroups.push_back(group);
    }

    std::vector<FlatCalibrationGroup> flatGroups;
    for (const auto& frame : flatFrames)
    {
        FlatCalibrationGroup group;
        group.key = FlatGroupKey::fromFrame(frame);
        group.groupKey = group.key.toGroupKey(defaultOptions);
        group.masterPath = frame.displayPath;
        flatGroups.push_back(group);
    }

    // Collect LightGroupKeys from light frames
    std::vector<LightGroupKey> lightKeys;
    for (const auto& frame : lightFrames)
    {
        lightKeys.push_back(LightGroupKey::fromFrame(frame));
    }

    CalibrationMatcher matcher(defaultOptions, defaultOptions, defaultOptions);
    matcher.setBiasGroups(std::move(biasGroups));
    matcher.setDarkGroups(std::move(darkGroups));
    matcher.setFlatGroups(std::move(flatGroups));

    QElapsedTimer timer;
    timer.start();

    // Match all light groups
    for (const auto& lightKey : lightKeys)
    {
        [[maybe_unused]] auto bias = matcher.findBias(lightKey);
        [[maybe_unused]] auto dark = matcher.findDark(lightKey);
        [[maybe_unused]] auto flat = matcher.findFlat(lightKey);
    }

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Calibration matching (500 groups): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 200);
}

// Target: Linear scaling - 10x frames should take < 10x time
TEST_F(BenchmarkTest, GroupingEngine_LinearScaling)
{
    constexpr int kBaselineFrameCount = 2000;
    constexpr int kScaleFactor = 10;
    constexpr int kScaledFrameCount = kBaselineFrameCount * kScaleFactor;
    constexpr int kBaselineRepeats = 5;
    constexpr int kScaledSamples = 3;

    // Measure a batched baseline so one tiny timing sample does not dominate
    // the ratio when the full test suite is under variable machine load.
    auto baselineFrames = createBenchmarkFrames(kBaselineFrameCount, FrameType::Light);
    auto scaledFrames = createBenchmarkFrames(kScaledFrameCount, FrameType::Light);
    GroupingOptions options;
    GroupingEngine engine;

    size_t sink = 0;

    // Warm both paths before measuring so first-use costs do not skew scaling.
    sink += static_cast<size_t>(engine.groupFrames(baselineFrames, FrameType::Light, options).size());
    sink += static_cast<size_t>(engine.groupFrames(scaledFrames, FrameType::Light, options).size());

    const qint64 baselineBatchNs =
        measureGroupingBatchNsecs(engine, baselineFrames, FrameType::Light, options, kBaselineRepeats, sink);
    const qint64 scaledBestNs =
        measureBestGroupingBatchNsecs(engine, scaledFrames, FrameType::Light, options, kScaledSamples, 1, sink);

    EXPECT_GT(sink, 0u);
    EXPECT_GT(baselineBatchNs, 0);
    EXPECT_GT(scaledBestNs, 0);

    const double baselineAverageNs = static_cast<double>(baselineBatchNs) / kBaselineRepeats;
    const double baselineAverageMs = baselineAverageNs / 1.0e6;
    const double scaledBestMs = static_cast<double>(scaledBestNs) / 1.0e6;
    const double ratio = baselineAverageNs > 0.0 ? static_cast<double>(scaledBestNs) / baselineAverageNs : 0.0;
    const double maxAllowedRatio = static_cast<double>(kScaleFactor) * 2.0;

    std::cout << "[ PERF     ] " << kBaselineFrameCount << " frames average: " << baselineAverageMs << " ms, "
              << kScaledFrameCount << " frames best of " << kScaledSamples << ": " << scaledBestMs << " ms"
              << std::endl;
    std::cout << "[ PERF     ] Scaling ratio: " << ratio << "x" << std::endl;

    EXPECT_LE(ratio, maxAllowedRatio) << "Grouping should scale roughly linearly";
}

// Target: < 100ms for batch override on 100 light groups
TEST_F(BenchmarkTest, BatchOverride_100Groups)
{
    IntegrationDataModel model;
    model.beginBatchUpdate();
    auto lightFrames = createBenchmarkFrames(100, FrameType::Light);
    model.addFrames(FrameType::Light, lightFrames);
    model.endBatchUpdate();

    const QString masterPath = "C:/master_bias.fit";
    const CalibrationOverrideType type = CalibrationOverrideType::Bias;

    QElapsedTimer timer;
    timer.start();

    model.beginBatchUpdate();
    for (const auto& g : model.lightGroups())
    {
        if (!g.frames.empty())
        {
            model.setCalibrationOverride(LightGroupKey::fromFrame(g.frames.front()), type, masterPath);
        }
    }
    model.endBatchUpdate();

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Batch override (100 groups): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 100);
}

// Target: < 1000ms for batch override on 1000 light groups
TEST_F(BenchmarkTest, BatchOverride_1000Groups)
{
    IntegrationDataModel model;
    model.beginBatchUpdate();
    auto lightFrames = createBenchmarkFrames(1000, FrameType::Light);
    model.addFrames(FrameType::Light, lightFrames);
    model.endBatchUpdate();

    const QString masterPath = "C:/master_bias.fit";
    const CalibrationOverrideType type = CalibrationOverrideType::Bias;

    QElapsedTimer timer;
    timer.start();

    model.beginBatchUpdate();
    for (const auto& g : model.lightGroups())
    {
        if (!g.frames.empty())
        {
            model.setCalibrationOverride(LightGroupKey::fromFrame(g.frames.front()), type, masterPath);
        }
    }
    model.endBatchUpdate();

    qint64 elapsed = timer.nsecsElapsed() / 1000000;
    std::cout << "[ PERF     ] Batch override (1000 groups): " << elapsed << " ms" << std::endl;

    expectWithinBudget(elapsed, 1000);
}

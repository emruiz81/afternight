// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Isolation tests for two engine contexts running domain work in one process at the same time.
//
// The runtime suites next door prove the property one layer at a time: an admission budget is per
// context, a memory resource accounts per context, two contexts do not share each other's threads.
// This file asks the question those cannot, because the answer is not in the runtime at all. An
// algorithm, an image, a codec or a library adapter that kept something of its own between calls
// would couple two contexts without any runtime type noticing, and the cases below drive real
// operations through both contexts at once and look for that coupling.
//
// The operation used as the instrument is the phase correlation, chosen because of what it crosses
// on the way: it takes working memory from its engine context, it runs pointwise stages on that
// context's threads, and it plans two transforms through the one FFTW planner this process has.
// That planner is the one piece of process-wide state the engine's own code serializes rather than
// owns - a library that keeps its wisdom globally and gives its callers no choice - so it is
// exactly where a coupling between two contexts would show up first if the serialization were
// wrong. The declared inventory of such state is in `build/scripts/tools/check_process_state.py`,
// and these cases are the behavioural half of it.
//
// The measurements are compared bit for bit rather than within a tolerance. Two contexts are not
// two numerical regimes: an operation that answers differently because another context existed is
// wrong however small the difference is, and a tolerance here would hide exactly the defect the
// file is looking for. What may legitimately differ - how many workers were admitted, how much
// memory was live - is asserted separately and per context.

#include <AfterNightEngine/Algorithms/Registration/PhaseCorrelation.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// The side of every frame these cases correlate. Large enough that the transform does real work and
/// small enough that hundreds of rounds of it on two threads stay well inside a test's time.
constexpr std::uint32_t kSide = 64u;

/// How many rounds the concurrent cases run.
///
/// The number is high because a race is not a property one round can have. Measured on the
/// development host with the FFTW planner lock deliberately removed: at eight rounds the cases
/// passed six times out of six, and at this count the process died in five runs out of six. All
/// eight cases together still take about fifty milliseconds, which is what makes a real race
/// detector affordable on every run rather than only under an instrumented build.
constexpr int kRounds = 400;

/// An engine context with a stated thread and memory budget.
///
/// @param concurrency how many threads the context owns.
/// @param budgetBytes what it may have outstanding at once.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u,
                                           std::uint64_t budgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits = an::defaultEngineLimits();
    options.limits.memoryBudgetBytes = budgetBytes;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// An execution over one engine context.
///
/// @param engine the context to lease from.
/// @param concurrency how many threads the operation asks for.
/// @param backend which backend to force, or `Automatic` to let dispatch decide.
/// @return the execution, which is valid or the case has already failed.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 2u,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;

    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::ExecutionContext{};
}

/// A planar single-channel Float32 layout of the fixed side.
[[nodiscard]] an::ImageLayout frameLayout()
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kSide, kSide, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = 1u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(kSide) * sizeof(float);
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * kSide, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// A frame of soft synthetic stars, displaced by a stated offset.
///
/// The content is deterministic, so two contexts correlating the same pair have one right answer to
/// agree on rather than two plausible ones.
///
/// @param offsetX how far every star is displaced to the right.
/// @param offsetY how far every star is displaced down.
/// @return the samples, row-major.
[[nodiscard]] std::vector<float> starField(double offsetX, double offsetY)
{
    std::vector<float> samples(static_cast<std::size_t>(kSide) * kSide, 0.05f);
    const double centres[][2] = {{14.0, 11.0}, {37.0, 9.0}, {22.0, 30.0}, {45.0, 41.0}, {9.0, 44.0}, {52.0, 24.0}};
    for (const auto& centre : centres)
    {
        const double starX = centre[0] + offsetX;
        const double starY = centre[1] + offsetY;
        for (std::uint32_t y = 0u; y < kSide; ++y)
        {
            for (std::uint32_t x = 0u; x < kSide; ++x)
            {
                const double dx = static_cast<double>(x) - starX;
                const double dy = static_cast<double>(y) - starY;
                const double radius = dx * dx + dy * dy;
                if (radius < 16.0)
                {
                    samples[static_cast<std::size_t>(y) * kSide + x] +=
                        static_cast<float>(0.8 * std::exp(-radius / 3.0));
                }
            }
        }
    }
    return samples;
}

/// The two frames every case correlates, built once per case.
struct FramePair
{
    std::vector<float> reference{starField(0.0, 0.0)};
    std::vector<float> target{starField(3.0, -2.0)};
    an::ImageLayout layout{frameLayout()};
};

/// Runs the instrument operation on one execution.
///
/// @param execution the operation.
/// @param frames the pair to correlate.
/// @return what the operation measured, or its refusal.
[[nodiscard]] an::Result<an::PhaseCorrelationResult> correlate(const an::ExecutionContext& execution,
                                                               const FramePair& frames)
{
    return an::computeImagePhaseCorrelation(execution, an::SampleType::Float32, frames.reference.data(), frames.layout,
                                            frames.target.data(), frames.layout);
}

/// Whether two measurements are the same answer, sample for sample.
///
/// @param left one measurement.
/// @param right the other.
/// @return true when every reported number is identical.
[[nodiscard]] bool sameAnswer(const an::PhaseCorrelationResult& left, const an::PhaseCorrelationResult& right)
{
    return left.translationX == right.translationX && left.translationY == right.translationY &&
           left.response == right.response && left.peakValue == right.peakValue && left.peakX == right.peakX &&
           left.peakY == right.peakY && left.precision == right.precision;
}

/// A diagnostic sink that records which operations reported to it.
class RecordingDiagnosticSink final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        ++m_count;
        m_operationIds.insert(event.operationId);
    }

    [[nodiscard]] std::uint64_t count() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_count;
    }

    [[nodiscard]] std::set<std::uint64_t> operationIds() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_operationIds;
    }

private:
    mutable std::mutex m_guard;
    std::uint64_t m_count = 0u;
    std::set<std::uint64_t> m_operationIds;
};

} // namespace

TEST(MultiInstanceIsolationTest, TwoContextsRunningOneOperationAtOnceAnswerWhatOneContextAloneAnswers)
{
    // The case the file exists for. The same pair is correlated alone, and then on two contexts
    // concurrently for eight rounds; every one of the sixteen concurrent answers must be the
    // solitary answer, bit for bit. A cache, a scratch buffer or a planner shared between the two
    // would show up here as a number that moves.
    const FramePair frames;

    const an::EngineContext alone = makeEngine();
    const an::Result<an::PhaseCorrelationResult> expected = correlate(makeExecution(alone), frames);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();

    const an::EngineContext first = makeEngine(2u);
    const an::EngineContext second = makeEngine(4u);

    std::atomic<int> disagreements{0};
    std::atomic<int> failures{0};
    const auto run = [&](const an::EngineContext& engine) {
        for (int round = 0; round < kRounds; ++round)
        {
            const an::Result<an::PhaseCorrelationResult> measured = correlate(makeExecution(engine), frames);
            if (!measured.hasValue())
            {
                failures.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (!sameAnswer(*measured, *expected))
            {
                disagreements.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::thread other([&]() { run(second); });
    run(first);
    other.join();

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0) << "an operation was refused only when a second "
                                                              "context existed";
    EXPECT_EQ(disagreements.load(std::memory_order_relaxed), 0)
        << "an operation answered differently because another engine context was running one too";
}

TEST(MultiInstanceIsolationTest, AnOperationsWorkingMemoryIsCountedOnlyInItsOwnContext)
{
    // Working memory comes from the engine context, so the context that did not run the operation
    // must not have paid for it. The live count returns to where it started, because the operation
    // releases what it took; the totals are what say it was ever taken at all.
    const FramePair frames;
    const an::EngineContext busy = makeEngine();
    const an::EngineContext idle = makeEngine();
    ASSERT_NE(busy.memoryResource(), nullptr);
    ASSERT_NE(idle.memoryResource(), nullptr);

    const an::MemoryCounters idleBefore = idle.memoryResource()->counters();
    const an::MemoryCounters busyBefore = busy.memoryResource()->counters();

    const an::Result<an::PhaseCorrelationResult> measured = correlate(makeExecution(busy), frames);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_GT(measured->workingBytes, 0u);

    const an::MemoryCounters busyAfter = busy.memoryResource()->counters();
    const an::MemoryCounters idleAfter = idle.memoryResource()->counters();

    EXPECT_GT(busyAfter.totalBytes, busyBefore.totalBytes) << "the operation took no memory from its own context";
    EXPECT_EQ(busyAfter.liveBytes, busyBefore.liveBytes) << "the operation kept memory after it finished";
    EXPECT_EQ(idleAfter.totalBytes, idleBefore.totalBytes)
        << "an operation in one context was charged to another context";
    EXPECT_EQ(idleAfter.peakBytes, idleBefore.peakBytes);
    EXPECT_EQ(idleAfter.deniedAllocations, idleBefore.deniedAllocations);
}

TEST(MultiInstanceIsolationTest, AContextTooSmallForTheWorkRefusesItsOwnAndNotTheOthers)
{
    // The other direction of the same property, and the one that matters more: a refusal is a
    // context's own answer. The starved context must refuse while the ordinary one beside it, at
    // the same moment, on the same input, succeeds - and the starved one's denied count must be the
    // only one that moved.
    const FramePair frames;
    const an::EngineContext starved = makeEngine(2u, 4096u);
    const an::EngineContext ordinary = makeEngine(2u);
    ASSERT_NE(starved.memoryResource(), nullptr);
    ASSERT_NE(ordinary.memoryResource(), nullptr);

    const an::MemoryCounters ordinaryBefore = ordinary.memoryResource()->counters();

    an::Status refused = an::Status::success();
    std::thread other([&]() {
        const an::Result<an::PhaseCorrelationResult> measured = correlate(makeExecution(starved), frames);
        refused = measured.hasValue() ? an::Status::success() : measured.status();
    });
    const an::Result<an::PhaseCorrelationResult> served = correlate(makeExecution(ordinary), frames);
    other.join();

    EXPECT_FALSE(refused.ok()) << "a context with four kilobytes of budget ran a transform";
    EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded) << refused.message();
    ASSERT_TRUE(served.hasValue()) << "a starved context refusing its own work refused another context's too: "
                                   << served.status().message();
    EXPECT_EQ(ordinary.memoryResource()->counters().deniedAllocations, ordinaryBefore.deniedAllocations)
        << "one context's refusal was counted against another";
    EXPECT_GT(starved.memoryResource()->counters().deniedAllocations, 0u);
}

TEST(MultiInstanceIsolationTest, EveryContextAndEveryOperationInThisProcessHasItsOwnIdentity)
{
    // Identity is the one thing the engine numbers across the process, and the reason it does is
    // here: two contexts and the operations inside them have to be separable by a sink that sees
    // both. Sixty-four executions are built from two contexts on two threads and no number may
    // repeat.
    const an::EngineContext first = makeEngine(2u);
    const an::EngineContext second = makeEngine(2u);
    EXPECT_NE(first.id(), second.id());

    std::mutex guard;
    std::set<std::uint64_t> identities;
    const auto run = [&](const an::EngineContext& engine) {
        for (int round = 0; round < 16; ++round)
        {
            const an::ExecutionContext execution = makeExecution(engine, 1u);
            const std::lock_guard<std::mutex> held(guard);
            identities.insert(execution.operationId());
        }
    };

    std::thread third([&]() { run(first); });
    std::thread fourth([&]() { run(second); });
    run(first);
    run(second);
    third.join();
    fourth.join();

    EXPECT_EQ(identities.size(), 64u) << "two operations in this process were given one identity";
}

TEST(MultiInstanceIsolationTest, ShuttingOneContextDownDoesNotDisturbWorkOnAnother)
{
    // Shutdown is the operation with the widest reach a context has, and it must reach exactly one
    // context. The work on the surviving context is started first, runs across the shutdown, and
    // has to produce the same answer it produces alone.
    const FramePair frames;
    const an::EngineContext reference = makeEngine();
    const an::Result<an::PhaseCorrelationResult> expected = correlate(makeExecution(reference), frames);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();

    an::EngineContext closing = makeEngine(2u);
    const an::EngineContext surviving = makeEngine(2u);

    std::atomic<bool> started{false};
    std::atomic<int> disagreements{0};
    std::atomic<int> failures{0};
    std::thread worker([&]() {
        for (int round = 0; round < kRounds; ++round)
        {
            started.store(true, std::memory_order_release);
            const an::Result<an::PhaseCorrelationResult> measured = correlate(makeExecution(surviving), frames);
            if (!measured.hasValue())
            {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
            else if (!sameAnswer(*measured, *expected))
            {
                disagreements.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    const an::Status shutdown = closing.shutdown();
    worker.join();

    EXPECT_TRUE(shutdown.ok()) << shutdown.message();
    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0) << "shutting one context down refused another's work";
    EXPECT_EQ(disagreements.load(std::memory_order_relaxed), 0)
        << "shutting one context down changed another context's answer";
}

TEST(MultiInstanceIsolationTest, AnImageHoldsOpenTheContextItsMemoryCameFromAndNoOther)
{
    // Storage takes a lease on the context whose resource it allocated from, which is what makes a
    // context with an image outstanding refuse to shut down. The property under test is that the
    // lease is one context's: the other context, holding nothing, shuts down at the same moment.
    an::EngineContext owning = makeEngine(2u);
    an::EngineContext empty = makeEngine(2u);

    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(64u, 64u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = sizeof(float),
                                         .samplesPerPixel = 1u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    an::Result<an::ImageStorageBytes> storage =
        an::ImageStorageBytes::create(owning, request, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(storage.hasValue()) << storage.status().message();

    EXPECT_EQ(owning.shutdown().code(), an::ErrorCode::Busy) << "a context with an image outstanding shut down";
    EXPECT_TRUE(empty.shutdown().ok()) << "a context holding nothing was kept open by another context's image";

    storage->release();
    EXPECT_TRUE(owning.shutdown().ok()) << "releasing the image did not release its context";
}

TEST(MultiInstanceIsolationTest, TheBackendEveryContextResolvesIsTheSameAnswer)
{
    // What this build contains and what this host supports are facts about neither context, so two
    // contexts must resolve the same backend and the same instruction set. Forcing one execution
    // onto the scalar reference is then the control: it changes that execution's answer and nothing
    // else's, which is what says the selection is per operation rather than a setting somewhere.
    const an::EngineContext first = makeEngine(2u);
    const an::EngineContext second = makeEngine(4u);

    const an::Result<an::BackendSelection> firstSelection = an::selectBackend(makeExecution(first));
    const an::Result<an::BackendSelection> secondSelection = an::selectBackend(makeExecution(second));
    ASSERT_TRUE(firstSelection.hasValue()) << firstSelection.status().message();
    ASSERT_TRUE(secondSelection.hasValue()) << secondSelection.status().message();
    EXPECT_TRUE(*firstSelection == *secondSelection) << "two engine contexts disagreed about what this build runs on";

    const an::Result<an::BackendSelection> forced =
        an::selectBackend(makeExecution(first, 2u, an::ComputeBackend::Reference));
    ASSERT_TRUE(forced.hasValue()) << forced.status().message();
    EXPECT_EQ(forced->backend, an::ComputeBackend::Reference);

    const an::Result<an::BackendSelection> after = an::selectBackend(makeExecution(second));
    ASSERT_TRUE(after.hasValue()) << after.status().message();
    EXPECT_TRUE(*after == *secondSelection) << "forcing one operation's backend changed another context's";
}

TEST(MultiInstanceIsolationTest, ASinkGivenToOneContextsOperationHearsNothingFromAnothers)
{
    // The engine reports through the sinks it was handed and says nothing otherwise, so an
    // execution built on another context - with no sink at all - must not find its way to this one.
    // The recorded identities are the second half of the claim: whatever arrived belongs to the
    // operation that was given the sink.
    const FramePair frames;
    const an::EngineContext listening = makeEngine(2u);
    const an::EngineContext silent = makeEngine(2u);

    RecordingDiagnosticSink sink;
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.diagnosticSink = &sink;
    an::Result<an::ExecutionContext> heard = an::ExecutionContext::create(listening, options);
    ASSERT_TRUE(heard.hasValue()) << heard.status().message();
    const std::uint64_t heardId = heard->operationId();

    const an::Result<an::PhaseCorrelationResult> first = correlate(*heard, frames);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    const std::uint64_t afterOwn = sink.count();
    ASSERT_GT(afterOwn, 0u) << "the instrument operation reports nothing, so this case could not tell a sink that "
                               "heard the wrong operation from one that heard nothing at all";

    const an::Result<an::PhaseCorrelationResult> second = correlate(makeExecution(silent), frames);
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    EXPECT_EQ(sink.count(), afterOwn) << "a sink heard an operation from a context it was never given to";
    for (const std::uint64_t identity : sink.operationIds())
    {
        EXPECT_EQ(identity, heardId) << "a report reached the wrong operation's sink";
    }
}

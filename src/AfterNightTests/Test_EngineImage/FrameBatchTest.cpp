// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FrameBatch.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{
[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u)
{
    auto made = an::EngineContext::create({.maxConcurrency = concurrency});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency,
                                                 std::stop_token stop = {})
{
    auto made = an::ExecutionContext::create(engine, {.stopToken = stop, .maxConcurrency = concurrency});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made ? std::move(*made) : an::ExecutionContext{};
}

class RecordingOperation final : public an::FrameBatchOperation
{
public:
    explicit RecordingOperation(std::uint64_t failingToken = (std::numeric_limits<std::uint64_t>::max)(),
                                std::stop_source* stop = nullptr)
        : m_failingToken(failingToken), m_stop(stop)
    {
    }

    an::Status run(const an::EngineContext&, const an::ExecutionContext&, const an::FrameBatchJob& job) override
    {
        ++calls;
        for (std::uint64_t index = 0u; index < 5000000u; ++index)
            observedCpuWork.fetch_add(index, std::memory_order_relaxed);
        if (m_stop != nullptr && job.token == 1u)
            m_stop->request_stop();
        if (job.token == m_failingToken)
            return an::Status::failure(an::ErrorCode::InvalidData);
        return an::Status::success();
    }

    std::atomic<std::uint64_t> calls{0u};
    std::atomic<std::uint64_t> observedCpuWork{0u};

private:
    std::uint64_t m_failingToken;
    std::stop_source* m_stop;
};

class ThrowingOperation final : public an::FrameBatchOperation
{
public:
    an::Status run(const an::EngineContext&, const an::ExecutionContext&, const an::FrameBatchJob&) override
    {
        throw std::runtime_error("callback defect");
    }
};

std::vector<an::FrameBatchJob> jobs()
{
    return {{.token = 0u, .inputPath = "in/zero.fit", .outputPath = "out/zero.fit"},
            {.token = 1u, .inputPath = "in/one.fit", .outputPath = "out/one.fit"},
            {.token = 2u, .inputPath = "in/two.fit", .outputPath = "out/two.fit"},
            {.token = 3u, .inputPath = "in/three.fit", .outputPath = "out/three.fit"}};
}
} // namespace

TEST(FrameBatchTest, ContinuePolicyReturnsEveryTokenPathAndPerFrameFailureInInputOrder)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, 4u);
    RecordingOperation operation(2u);
    const auto input = jobs();
    auto report = an::executeFrameBatch(engine, execution, input, operation,
                                        {.failurePolicy = an::FrameBatchFailurePolicy::Continue});
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    ASSERT_EQ(report->completions.size(), input.size());
    EXPECT_EQ(report->counters.jobsSubmitted, 4u);
    EXPECT_EQ(report->counters.jobsStarted, 4u);
    EXPECT_EQ(report->counters.jobsSucceeded, 3u);
    EXPECT_EQ(report->counters.jobsFailed, 1u);
    EXPECT_EQ(report->counters.jobsNotStarted, 0u);
    EXPECT_GT(report->counters.plannedWorkers, 0u);
    EXPECT_LE(report->counters.plannedWorkers, input.size());
    EXPECT_GT(report->counters.workerCpuNanoseconds, 0u);
    EXPECT_EQ(report->outcome.code(), an::ErrorCode::InvalidData);
    std::uint64_t measuredCpuNanoseconds = 0u;
    for (std::size_t index = 0u; index < input.size(); ++index)
    {
        EXPECT_EQ(report->completions[index].jobIndex, index);
        EXPECT_EQ(report->completions[index].token, input[index].token);
        EXPECT_EQ(report->completions[index].inputPath, input[index].inputPath);
        EXPECT_EQ(report->completions[index].outputPath, input[index].outputPath);
        EXPECT_LT(report->completions[index].workerSlot, report->counters.plannedWorkers);
        measuredCpuNanoseconds += report->completions[index].workerCpuNanoseconds;
    }
    EXPECT_EQ(report->counters.workerCpuNanoseconds, measuredCpuNanoseconds);
    EXPECT_EQ(report->completions[2].status.code(), an::ErrorCode::InvalidData);
    execution = {};
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(FrameBatchTest, StopPolicyLeavesLaterSerialJobsExplicitlyNotStarted)
{
    an::EngineContext engine = makeEngine(1u);
    an::ExecutionContext execution = makeExecution(engine, 1u);
    RecordingOperation operation(1u);
    const auto input = jobs();
    auto report = an::executeFrameBatch(engine, execution, input, operation);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    ASSERT_EQ(report->completions.size(), 2u);
    EXPECT_EQ(report->completions[0].token, 0u);
    EXPECT_EQ(report->completions[1].token, 1u);
    EXPECT_EQ(report->outcome.code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(report->counters.jobsNotStarted, 2u);
    execution = {};
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(FrameBatchTest, CancellationDrainsEveryCompletionThatAlreadyPublished)
{
    an::EngineContext engine = makeEngine(1u);
    std::stop_source stop;
    an::ExecutionContext execution = makeExecution(engine, 1u, stop.get_token());
    RecordingOperation operation((std::numeric_limits<std::uint64_t>::max)(), &stop);
    const auto input = jobs();
    auto report = an::executeFrameBatch(engine, execution, input, operation,
                                        {.failurePolicy = an::FrameBatchFailurePolicy::Continue});
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    ASSERT_EQ(report->completions.size(), 2u);
    EXPECT_EQ(report->completions[0].token, 0u);
    EXPECT_EQ(report->completions[1].token, 1u);
    EXPECT_TRUE(report->completions[1].status.ok());
    EXPECT_EQ(report->outcome.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(report->counters.jobsNotStarted, 2u);
    execution = {};
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(FrameBatchTest, PreflightRefusalsRunNoJob)
{
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, 2u);
    RecordingOperation operation;
    const auto input = jobs();
    auto bounded = an::executeFrameBatch(engine, execution, input, operation, {.maximumJobs = 3u});
    EXPECT_EQ(bounded.status().code(), an::ErrorCode::LimitExceeded);
    std::vector<an::FrameBatchJob> malformed{{.token = 0u, .inputPath = {}, .outputPath = "output.fit"}};
    auto invalid = an::executeFrameBatch(engine, execution, malformed, operation);
    EXPECT_EQ(invalid.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(operation.calls.load(), 0u);
    execution = {};
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(FrameBatchTest, CallbackExceptionsBecomePerFrameFailuresAndNeverLeaveTheWorkerEntry)
{
    an::EngineContext engine = makeEngine(1u);
    an::ExecutionContext execution = makeExecution(engine, 1u);
    ThrowingOperation operation;
    const std::vector<an::FrameBatchJob> input{{.token = 7u, .inputPath = "input.fit", .outputPath = "output.fit"}};
    auto report = an::executeFrameBatch(engine, execution, input, operation);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    ASSERT_EQ(report->completions.size(), 1u);
    EXPECT_EQ(report->completions.front().status.code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(report->outcome.code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(report->counters.jobsFailed, 1u);
    execution = {};
    EXPECT_TRUE(engine.shutdown().ok());
}

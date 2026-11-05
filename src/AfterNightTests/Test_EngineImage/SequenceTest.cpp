// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/FrameSink.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
an::RawFrameDescriptor frameDescription(std::uint32_t width = 3u)
{
    return *an::RawFrameDescriptor::create(
        {.extent = *an::ImageExtent::create(width, 2u, 1u, an::defaultEngineLimits()),
         .sampleFormat = {2u, an::RawSampleClass::UnsignedInteger},
         .byteOrder = an::ByteOrder::Big,
         .channelSchema = an::ChannelSchema::Mono,
         .colorSpace = an::ColorSpace::Unspecified,
         .numericDomain = an::NumericDomain::RawSensorCode,
         .cfa = {},
         .coordinateOrigin = an::CoordinateOrigin::TopLeft,
         .pixelAspectRatio = 1.0});
}

an::SequenceDescriptor sequenceDescription()
{
    return {.format = an::SequenceFormat::Ser,
            .fixedFrame = frameDescription(),
            .frameCount = 3u,
            .timeBase = an::SequenceRate{1u, 1000u},
            .nominalFrameRate = an::SequenceRate{25u, 1u},
            .durationTicks = 120,
            .timestampsRequired = true,
            .seekByIndex = true,
            .seekByTime = true};
}

struct BackendControl
{
    std::uint64_t reads = 0u;
    std::uint64_t seeks = 0u;
    std::uint64_t appends = 0u;
    std::uint64_t finishes = 0u;
    std::uint64_t closes = 0u;
    std::uint64_t destructions = 0u;
    std::uint64_t available = 3u;
    std::uint64_t bytes = 0u;
    int fault = 0;
    bool throwClose = false;
    bool badClose = false;
    bool nested = false;
    std::atomic<std::uint64_t> nestedItems = 0u;
    std::stop_source* stop = nullptr;
    const an::EngineContext* allocationEngine = nullptr;
    std::shared_ptr<std::array<std::byte, 24u>> external;
};

an::Result<an::SequenceFrame> makeFrame(const an::EngineContext& engine, std::uint64_t index,
                                        BackendControl* control = nullptr)
{
    auto descriptor = frameDescription(control && control->fault == 2 ? 4u : 3u);
    const auto layout =
        *an::ImageLayout::createWithStrides({descriptor.extent(), 2u, 1u, 1u, 1u}, 12u, 24u, engine.limits());
    auto buffer =
        control && control->external
            ? (control->fault == 3
                   ? an::SampleBuffer::borrowConst(std::span<const std::byte>{*control->external})
                   : an::SampleBuffer::external(an::SampleBufferOwnership::DecoderOwned, control->external->data(),
                                                control->external->size(), control->external, false))
            : an::SampleBuffer::createOwned(control && control->allocationEngine ? *control->allocationEngine : engine,
                                            layout, an::StorageInitialization::Zeroed);
    if (!buffer.hasValue())
        return buffer.status();
    auto raw = an::RawFrame::create(descriptor, layout, std::move(*buffer));
    if (!raw.hasValue())
        return raw.status();
    an::SequenceFrame frame{std::move(*raw), {}, {index, static_cast<std::int64_t>(index * 40u), 40}};
    if (control)
    {
        if (control->fault == 1)
            ++frame.position.index;
        if (control->fault == 4)
            frame.metadata = an::ImageMetadata::create(
                {}, {}, {}, {}, {}, *an::OutputIntent::create({.sampleType = an::SampleType::UInt16}));
        if (control->fault == 5)
            frame.position.presentationTicks.reset();
        if (control->fault == 6)
            frame.position.presentationTicks = -1;
        if (control->fault == 7)
            frame.position.durationTicks = (std::numeric_limits<std::int64_t>::max)();
    }
    return frame;
}

class SourceBackend final : public an::FrameSourceBackend
{
public:
    explicit SourceBackend(std::shared_ptr<BackendControl> control) : m_control(std::move(control)) {}
    ~SourceBackend() override
    {
        ++m_control->destructions;
    }
    an::Result<an::SequenceReadResult> read(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                            std::uint64_t index) override
    {
        ++m_control->reads;
        if (m_control->fault == 8)
            throw std::runtime_error("decoder exception");
        if (m_control->fault == 9)
            throw std::bad_alloc();
        if (m_control->fault == 10)
            return an::Status::failure(an::ErrorCode::ExternalFailure);
        if (m_control->nested)
        {
            const auto status = an::parallelFor(execution, 100u, {1u, 1u}, [&](an::TaskRange range) {
                m_control->nestedItems.fetch_add(range.end - range.begin);
                return an::Status::success();
            });
            if (status.failed())
                return status;
        }
        if (m_control->stop)
            m_control->stop->request_stop();
        if (index >= m_control->available)
            return an::SequenceReadResult{};
        auto frame = makeFrame(engine, index, m_control.get());
        if (!frame.hasValue())
            return frame.status();
        return an::SequenceReadResult{std::move(*frame)};
    }
    an::Result<an::SequencePosition> seek(const an::ExecutionContext&, const an::SequenceSeekRequest& request) override
    {
        ++m_control->seeks;
        if (m_control->fault == 8)
            throw 19;
        if (m_control->fault == 10)
            return an::Status::failure(an::ErrorCode::ExternalFailure);
        if (m_control->stop)
            m_control->stop->request_stop();
        if (m_control->fault == 12)
            return an::SequencePosition{0u, 120, {}};
        const auto index =
            request.kind == an::SequenceSeekKind::Index
                ? request.index
                : static_cast<std::uint64_t>(
                      (request.ticks + (request.kind == an::SequenceSeekKind::TimeAtOrAfter ? 39 : 0)) / 40);
        return an::SequencePosition{index + (m_control->fault == 1 ? 1u : 0u),
                                    m_control->fault == 11 ? std::optional<std::int64_t>{}
                                                           : static_cast<std::int64_t>(index * 40u),
                                    {}};
    }
    an::Status close() override
    {
        ++m_control->closes;
        if (m_control->throwClose)
            throw 7;
        return m_control->badClose ? an::Status::failure(an::ErrorCode::ExternalFailure) : an::Status::success();
    }

private:
    std::shared_ptr<BackendControl> m_control;
};

class SinkBackend final : public an::FrameSinkBackend
{
public:
    explicit SinkBackend(std::shared_ptr<BackendControl> control) : m_control(std::move(control)) {}
    ~SinkBackend() override
    {
        ++m_control->destructions;
    }
    an::Result<an::SequenceWriteResult> append(const an::EngineContext&, const an::ExecutionContext&,
                                               const an::SequenceFrame& frame) override
    {
        ++m_control->appends;
        m_control->bytes += frame.frame.layout()->totalBytes();
        if (m_control->stop)
            m_control->stop->request_stop();
        return report();
    }
    an::Result<an::SequenceWriteResult> finish(const an::ExecutionContext&) override
    {
        ++m_control->finishes;
        m_control->bytes += 8u;
        if (m_control->stop)
            m_control->stop->request_stop();
        return report();
    }
    an::Status close() override
    {
        ++m_control->closes;
        if (m_control->throwClose)
            throw 7;
        return m_control->badClose ? an::Status::failure(an::ErrorCode::ExternalFailure) : an::Status::success();
    }

private:
    an::Result<an::SequenceWriteResult> report()
    {
        if (m_control->fault == 8)
            throw std::runtime_error("encoder exception");
        if (m_control->fault == 9)
            throw std::bad_alloc();
        if (m_control->fault == 10)
            return an::Status::failure(an::ErrorCode::ExternalFailure);
        return an::SequenceWriteResult{m_control->appends + (m_control->fault == 1 ? 1u : 0u),
                                       m_control->fault == 2 ? 0u : m_control->bytes};
    }
    std::shared_ptr<BackendControl> m_control;
};

class SequenceTest : public testing::Test
{
protected:
    void SetUp() override
    {
        auto created = an::EngineContext::create({.maxConcurrency = 2u});
        ASSERT_TRUE(created.hasValue());
        engine = std::move(*created);
        auto operation = an::ExecutionContext::create(engine, {.stopToken = stop.get_token(), .maxConcurrency = 1u});
        ASSERT_TRUE(operation.hasValue());
        execution = std::move(*operation);
    }
    an::FrameSource source(an::SequenceDescriptor descriptor = sequenceDescription())
    {
        auto value = an::FrameSource::create(engine, descriptor, std::make_unique<SourceBackend>(control));
        EXPECT_TRUE(value.hasValue()) << static_cast<int>(value.code());
        return value.hasValue() ? std::move(*value) : an::FrameSource{};
    }
    an::FrameSink sink(std::optional<std::uint64_t> count = 3u)
    {
        auto descriptor = sequenceDescription();
        descriptor.seekByIndex = false;
        descriptor.seekByTime = false;
        descriptor.frameCount = count;
        auto value = an::FrameSink::create(engine, descriptor, std::make_unique<SinkBackend>(control));
        EXPECT_TRUE(value.hasValue()) << static_cast<int>(value.code());
        return value.hasValue() ? std::move(*value) : an::FrameSink{};
    }
    std::stop_source stop;
    an::EngineContext engine;
    an::ExecutionContext execution;
    std::shared_ptr<BackendControl> control = std::make_shared<BackendControl>();
};

TEST_F(SequenceTest, ReadsExactFramesWithoutChangingEndianOrPaddingAndRepeatsEof)
{
    auto input = source();
    for (std::uint64_t index = 0; index != 3u; ++index)
    {
        auto result = input.read(engine, execution);
        ASSERT_TRUE(result.hasValue());
        ASSERT_TRUE(result->frame);
        EXPECT_EQ(result->frame->position.index, index);
        EXPECT_EQ(result->frame->frame.descriptor()->byteOrder(), an::ByteOrder::Big);
        EXPECT_EQ(result->frame->frame.layout()->rowStrideBytes(), 12u);
        EXPECT_EQ(result->frame->frame.row(0, 0)->size(), 6u);
    }
    EXPECT_FALSE(input.read(engine, execution)->frame);
    EXPECT_FALSE(input.read(engine, execution)->frame);
    EXPECT_EQ(control->reads, 3u);
    EXPECT_EQ(input.state(), an::SequenceState::EndOfStream);
}

TEST_F(SequenceTest, EmptySourceDoesNotDecodeAndUnknownLengthObservesEofOnce)
{
    auto descriptor = sequenceDescription();
    descriptor.frameCount = 0u;
    auto empty = source(descriptor);
    EXPECT_FALSE(empty.read(engine, execution)->frame);
    EXPECT_EQ(control->reads, 0u);
    descriptor.frameCount.reset();
    auto input = source(descriptor);
    for (int index = 0; index != 3; ++index)
        ASSERT_TRUE(input.read(engine, execution)->frame);
    EXPECT_FALSE(input.read(engine, execution)->frame);
    EXPECT_FALSE(input.read(engine, execution)->frame);
    EXPECT_EQ(control->reads, 4u);
}

TEST_F(SequenceTest, PrematureEofIsTypedFailureAndCannotResume)
{
    control->available = 0u;
    auto input = source();
    auto result = input.read(engine, execution);
    EXPECT_EQ(an::sequenceFailureReason(result.status()), an::SequenceFailure::UnexpectedEnd);
    EXPECT_EQ(input.state(), an::SequenceState::Failed);
    EXPECT_EQ(input.nextIndex(), 0u);
    EXPECT_FALSE(input.read(engine, execution).hasValue());
    EXPECT_EQ(control->reads, 1u);
}

TEST_F(SequenceTest, ExactAndTimeSeeksReportActualPositionAndReopenEof)
{
    auto input = source();
    ASSERT_TRUE(input.seek(execution, {.index = 3u}).hasValue());
    EXPECT_EQ(input.state(), an::SequenceState::EndOfStream);
    auto after = input.seek(execution, {.kind = an::SequenceSeekKind::TimeAtOrAfter, .ticks = 41});
    ASSERT_TRUE(after.hasValue());
    EXPECT_EQ(after->index, 2u);
    EXPECT_EQ(after->presentationTicks, 80);
    EXPECT_EQ(input.read(engine, execution)->frame->position.index, 2u);
    auto before = input.seek(execution, {.kind = an::SequenceSeekKind::TimeAtOrBefore, .ticks = 41});
    ASSERT_TRUE(before.hasValue());
    EXPECT_EQ(before->index, 1u);
    EXPECT_EQ(input.read(engine, execution)->frame->position.index, 1u);
    ASSERT_TRUE(input.seek(execution, {}).hasValue());
    EXPECT_EQ(input.read(engine, execution)->frame->position.index, 0u);
}

TEST_F(SequenceTest, InvalidSeekDoesNotCallBackendOrChangeState)
{
    auto input = source();
    for (const auto request : {an::SequenceSeekRequest{.index = 4u}, an::SequenceSeekRequest{.index = 1u, .ticks = 1},
                               an::SequenceSeekRequest{.kind = an::SequenceSeekKind::TimeAtOrAfter, .ticks = -1},
                               an::SequenceSeekRequest{.kind = an::SequenceSeekKind::TimeAtOrBefore, .ticks = 120},
                               an::SequenceSeekRequest{.kind = static_cast<an::SequenceSeekKind>(99)}})
        EXPECT_FALSE(input.seek(execution, request).hasValue());
    EXPECT_EQ(control->seeks, 0u);
    EXPECT_EQ(input.state(), an::SequenceState::Open);
    auto descriptor = sequenceDescription();
    descriptor.seekByIndex = false;
    descriptor.seekByTime = false;
    auto sequential = source(descriptor);
    EXPECT_EQ(an::sequenceFailureReason(sequential.seek(execution, {}).status()), an::SequenceFailure::UnsupportedSeek);
}

TEST_F(SequenceTest, CancelBeforeCallbackAndDeadlineLeaveSourceOpen)
{
    auto input = source();
    auto expired =
        an::ExecutionContext::create(engine, {.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1)});
    ASSERT_TRUE(expired.hasValue());
    EXPECT_EQ(input.read(engine, *expired).code(), an::ErrorCode::DeadlineExceeded);
    stop.request_stop();
    EXPECT_EQ(input.read(engine, execution).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(input.seek(execution, {}).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(control->reads, 0u);
    EXPECT_EQ(control->seeks, 0u);
    EXPECT_EQ(input.state(), an::SequenceState::Open);
    EXPECT_TRUE(input.close().ok());
}

TEST_F(SequenceTest, CancellationInsideReadDiscardsCompleteFrameAndPoisonsSession)
{
    control->stop = &stop;
    auto input = source();
    EXPECT_EQ(input.read(engine, execution).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(input.state(), an::SequenceState::Failed);
    EXPECT_EQ(input.nextIndex(), 0u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}

TEST_F(SequenceTest, SeekFailureOrCancellationDoesNotExposeAnAdvancedPosition)
{
    for (const int fault : {1, 8, 10})
    {
        control->fault = fault;
        auto input = source();
        EXPECT_FALSE(input.seek(execution, {.index = 1u}).hasValue());
        EXPECT_EQ(input.state(), an::SequenceState::Failed);
        EXPECT_EQ(input.nextIndex(), 0u);
    }
    control->fault = 0;
    control->stop = &stop;
    auto input = source();
    EXPECT_EQ(input.seek(execution, {.index = 1u}).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(input.state(), an::SequenceState::Failed);
}

class SequenceReadFaultTest : public SequenceTest, public testing::WithParamInterface<int>
{
};
TEST_P(SequenceReadFaultTest, InvalidCallbackResultIsTerminalAndReleasesFrame)
{
    control->fault = GetParam();
    if (GetParam() == 3)
        control->external = std::make_shared<std::array<std::byte, 24u>>();
    auto input = source();
    const auto result = input.read(engine, execution);
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(input.state(), an::SequenceState::Failed);
    EXPECT_EQ(input.nextIndex(), 0u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    if (GetParam() == 8)
        EXPECT_EQ(result.code(), an::ErrorCode::ExternalFailure);
    if (GetParam() == 9)
        EXPECT_EQ(result.code(), an::ErrorCode::AllocationFailed);
    if (GetParam() == 10)
        EXPECT_EQ(result.status().cause().code(), an::ErrorCode::ExternalFailure);
}
INSTANTIATE_TEST_SUITE_P(Boundary, SequenceReadFaultTest, testing::Range(1, 11));

TEST_F(SequenceTest, DecoderTokenKeepsFrameAliveAcrossSeekAndClose)
{
    control->external = std::make_shared<std::array<std::byte, 24u>>();
    (*control->external)[0] = std::byte{0x42};
    std::weak_ptr<std::array<std::byte, 24u>> lifetime = control->external;
    auto input = source();
    auto result = input.read(engine, execution);
    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(input.seek(execution, {}).hasValue());
    ASSERT_TRUE(input.close().ok());
    control->external.reset();
    EXPECT_FALSE(lifetime.expired());
    EXPECT_EQ(result->frame->frame.bytes()[0], std::byte{0x42});
    result->frame.reset();
    EXPECT_TRUE(lifetime.expired());
}

TEST_F(SequenceTest, RejectsForeignExecutionsAndOwnedFrameBudgets)
{
    auto foreign = an::EngineContext::create({.maxConcurrency = 1u});
    auto operation = an::ExecutionContext::create(*foreign, {});
    auto input = source();
    EXPECT_EQ(input.read(engine, *operation).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(input.read(*foreign, execution).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(input.seek(*operation, {}).code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(control->reads, 0u);
    control->allocationEngine = &*foreign;
    EXPECT_EQ(an::sequenceFailureReason(input.read(engine, execution).status()),
              an::SequenceFailure::ForeignAllocation);
    EXPECT_EQ(foreign->memoryResource()->counters().liveBytes, 0u);
}

TEST_F(SequenceTest, VariableFrameDescriptionsRemainExplicit)
{
    auto descriptor = sequenceDescription();
    descriptor.fixedFrame.reset();
    control->fault = 2;
    auto input = source(descriptor);
    auto frame = input.read(engine, execution);
    ASSERT_TRUE(frame.hasValue());
    EXPECT_EQ(frame->frame->frame.descriptor()->extent().width(), 4u);
}

TEST_F(SequenceTest, SourceAndSinkLeasesDoNotReserveThreadsAndSurviveEngineMove)
{
    execution = {};
    auto input = source();
    auto output = sink();
    EXPECT_EQ(engine.outstandingLeases(), 2u);
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    auto moved = std::move(engine);
    auto operation = an::ExecutionContext::create(moved, {.maxConcurrency = 2u});
    ASSERT_TRUE(operation.hasValue());
    EXPECT_EQ(operation->admittedConcurrency(), 2u);
    EXPECT_TRUE(input.read(moved, *operation).hasValue());
    EXPECT_TRUE(input.close().ok());
    EXPECT_TRUE(output.close().ok());
    *operation = {};
    EXPECT_TRUE(moved.shutdown().ok());
}

TEST_F(SequenceTest, SinkAppendsFinalizesOnceAndNeverFinishesOnDestruction)
{
    {
        auto output = sink();
        EXPECT_FALSE(output.finish(execution).hasValue());
        EXPECT_EQ(control->finishes, 0u);
        for (std::uint64_t index = 0u; index != 3u; ++index)
        {
            auto frame = makeFrame(engine, index);
            const auto report = output.append(engine, execution, *frame);
            ASSERT_TRUE(report.hasValue());
            EXPECT_EQ(report->framesWritten, index + 1u);
        }
        EXPECT_FALSE(output.append(engine, execution, *makeFrame(engine, 3u)).hasValue());
        auto final = output.finish(execution);
        ASSERT_TRUE(final.hasValue());
        EXPECT_EQ(final->framesWritten, 3u);
        EXPECT_EQ(final->bytesWritten, 62u);
        EXPECT_EQ(output.state(), an::SequenceState::Finalized);
        stop.request_stop();
        EXPECT_TRUE(output.finish(execution).hasValue());
        EXPECT_EQ(control->finishes, 1u);
    }
    EXPECT_EQ(control->closes, 1u);
    {
        auto output = sink();
    }
    EXPECT_EQ(control->finishes, 1u);
    EXPECT_EQ(control->closes, 2u);
}

TEST_F(SequenceTest, SinkInputValidationLeavesWriterUsable)
{
    auto output = sink();
    auto frame = makeFrame(engine, 1u);
    EXPECT_FALSE(output.append(engine, execution, *frame).hasValue());
    frame->position.index = 0u;
    frame->position.presentationTicks = -1;
    EXPECT_FALSE(output.append(engine, execution, *frame).hasValue());
    frame->position.presentationTicks = 0;
    frame->position.durationTicks = 0;
    EXPECT_FALSE(output.append(engine, execution, *frame).hasValue());
    EXPECT_EQ(control->appends, 0u);
    EXPECT_EQ(output.state(), an::SequenceState::Open);
    frame->position.durationTicks = 40;
    EXPECT_TRUE(output.append(engine, execution, *frame).hasValue());
    frame->position.index = 1u;
    frame->position.presentationTicks = -1;
    EXPECT_FALSE(output.append(engine, execution, *frame).hasValue());
}

class SequenceWriteFaultTest : public SequenceTest, public testing::WithParamInterface<int>
{
};
TEST_P(SequenceWriteFaultTest, FailedAppendPreservesPublicProgressAndCannotFinish)
{
    auto output = sink();
    ASSERT_TRUE(output.append(engine, execution, *makeFrame(engine, 0u)).hasValue());
    control->fault = GetParam();
    EXPECT_FALSE(output.append(engine, execution, *makeFrame(engine, 1u)).hasValue());
    EXPECT_EQ(output.progress().framesWritten, 1u);
    EXPECT_EQ(output.progress().bytesWritten, 18u);
    EXPECT_EQ(output.state(), an::SequenceState::Failed);
    EXPECT_FALSE(output.finish(execution).hasValue());
    EXPECT_EQ(control->finishes, 0u);
}
TEST_P(SequenceWriteFaultTest, FailedFinishIsTerminal)
{
    auto output = sink(1u);
    ASSERT_TRUE(output.append(engine, execution, *makeFrame(engine, 0u)).hasValue());
    control->fault = GetParam();
    EXPECT_FALSE(output.finish(execution).hasValue());
    EXPECT_EQ(output.state(), an::SequenceState::Failed);
    EXPECT_EQ(output.progress().bytesWritten, 18u);
}
INSTANTIATE_TEST_SUITE_P(Boundary, SequenceWriteFaultTest, testing::Values(1, 2, 8, 9, 10));

TEST_F(SequenceTest, StopDuringAppendFailsButSuccessfulFinishWinsLateStop)
{
    auto output = sink();
    control->stop = &stop;
    EXPECT_EQ(output.append(engine, execution, *makeFrame(engine, 0u)).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(output.state(), an::SequenceState::Failed);
    EXPECT_EQ(output.progress().framesWritten, 0u);
    EXPECT_TRUE(output.close().ok());
    control = std::make_shared<BackendControl>();
    std::stop_source finishStop;
    auto operation = an::ExecutionContext::create(engine, {.stopToken = finishStop.get_token()});
    auto finalOutput = sink(0u);
    control->stop = &finishStop;
    EXPECT_TRUE(finalOutput.finish(*operation).hasValue());
    EXPECT_TRUE(finishStop.stop_requested());
    EXPECT_EQ(finalOutput.state(), an::SequenceState::Finalized);
}

TEST_F(SequenceTest, PreCancelledSinkDoesNotWriteOrFinalize)
{
    auto output = sink(std::nullopt);
    stop.request_stop();
    EXPECT_EQ(output.append(engine, execution, *makeFrame(engine, 0u)).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(output.finish(execution).code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(control->appends, 0u);
    EXPECT_EQ(control->finishes, 0u);
    EXPECT_EQ(output.state(), an::SequenceState::Open);
}

TEST_F(SequenceTest, CloseFailureAndThrownCleanupStillReleaseEverythingOnce)
{
    for (const bool throws : {false, true})
    {
        control->throwClose = throws;
        control->badClose = !throws;
        auto input = source();
        auto output = sink();
        const auto before = control->destructions;
        EXPECT_TRUE(input.close().failed());
        EXPECT_TRUE(output.close().failed());
        EXPECT_EQ(control->destructions, before + 2u);
        EXPECT_EQ(input.state(), an::SequenceState::Closed);
        EXPECT_EQ(output.state(), an::SequenceState::Closed);
        EXPECT_TRUE(input.close().ok());
        EXPECT_TRUE(output.close().ok());
    }
    EXPECT_EQ(control->closes, 4u);
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}

TEST_F(SequenceTest, MoveAssignmentClosesPreviousSessionAndMovedHandlesRefuse)
{
    auto first = source();
    auto second = source();
    first = std::move(second);
    EXPECT_EQ(control->closes, 1u);
    EXPECT_EQ(second.state(), an::SequenceState::Closed);
    EXPECT_FALSE(second.read(engine, execution).hasValue());
    EXPECT_EQ(second.descriptor(), nullptr);
    auto output = sink();
    auto other = sink();
    output = std::move(other);
    EXPECT_EQ(control->closes, 2u);
    EXPECT_EQ(other.state(), an::SequenceState::Closed);
    EXPECT_FALSE(other.finish(execution).hasValue());
}

TEST_F(SequenceTest, TwoIndependentSourcesCanRunConcurrentlyWithNestedScheduledWork)
{
    control->nested = true;
    auto first = source();
    auto secondControl = std::make_shared<BackendControl>();
    secondControl->nested = true;
    auto second =
        an::FrameSource::create(engine, sequenceDescription(), std::make_unique<SourceBackend>(secondControl));
    auto operation = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
    std::atomic<int> completed = 0;
    std::thread worker([&] {
        if (first.read(engine, execution).hasValue())
            ++completed;
    });
    if (second->read(engine, *operation).hasValue())
        ++completed;
    worker.join();
    EXPECT_EQ(completed, 2);
    EXPECT_EQ(control->nestedItems, 100u);
    EXPECT_EQ(secondControl->nestedItems, 100u);
}

TEST(SequenceDescriptorTest, RejectsIncoherentTimeline)
{
    an::SequenceDescriptor descriptor;
    EXPECT_TRUE(an::validateSequenceDescriptor(descriptor).ok());
    descriptor.seekByTime = true;
    EXPECT_EQ(an::sequenceFailureReason(an::validateSequenceDescriptor(descriptor)),
              an::SequenceFailure::InvalidDescriptor);
    descriptor.timeBase = an::SequenceRate{1u, 1000u};
    EXPECT_TRUE(an::validateSequenceDescriptor(descriptor).ok());
    descriptor.timeBase->denominator = 0u;
    EXPECT_FALSE(an::validateSequenceDescriptor(descriptor).ok());
}

TEST(SequenceDescriptorTest, ValidatesEveryFormatAndBothRationalFields)
{
    for (std::uint8_t value = 0u; value != 7u; ++value)
    {
        auto descriptor = sequenceDescription();
        descriptor.format = static_cast<an::SequenceFormat>(value);
        EXPECT_TRUE(an::validateSequenceDescriptor(descriptor).ok());
    }
    auto descriptor = sequenceDescription();
    descriptor.format = static_cast<an::SequenceFormat>(255);
    EXPECT_FALSE(an::validateSequenceDescriptor(descriptor).ok());
    descriptor = sequenceDescription();
    descriptor.timeBase->numerator = 0u;
    EXPECT_FALSE(an::validateSequenceDescriptor(descriptor).ok());
    descriptor = sequenceDescription();
    descriptor.nominalFrameRate->denominator = 0u;
    EXPECT_FALSE(an::validateSequenceDescriptor(descriptor).ok());
    descriptor.nominalFrameRate = an::SequenceRate{0u, 1u};
    EXPECT_FALSE(an::validateSequenceDescriptor(descriptor).ok());
    descriptor = sequenceDescription();
    descriptor.durationTicks = -1;
    EXPECT_FALSE(an::validateSequenceDescriptor(descriptor).ok());
}

TEST_F(SequenceTest, InvalidFactoriesDestroyBackendWithoutLeakingLease)
{
    const auto leases = engine.outstandingLeases();
    EXPECT_FALSE(an::FrameSource::create(engine, {}, nullptr).hasValue());
    EXPECT_FALSE(an::FrameSink::create(engine, {}, nullptr).hasValue());
    auto invalid = sequenceDescription();
    invalid.timeBase.reset();
    EXPECT_FALSE(an::FrameSource::create(engine, invalid, std::make_unique<SourceBackend>(control)).hasValue());
    EXPECT_FALSE(
        an::FrameSink::create(engine, sequenceDescription(), std::make_unique<SinkBackend>(control)).hasValue());
    EXPECT_EQ(control->destructions, 2u);
    EXPECT_EQ(engine.outstandingLeases(), leases);
}

TEST_F(SequenceTest, GeometryAndSampleAllocationHonorContextLimits)
{
    auto limits = an::defaultEngineLimits();
    limits.maxImageWidth = 2u;
    auto small = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
    EXPECT_EQ(an::FrameSource::create(*small, sequenceDescription(), std::make_unique<SourceBackend>(control)).code(),
              an::ErrorCode::LimitExceeded);
    limits = an::defaultEngineLimits();
    limits.memoryBudgetBytes = 17u;
    auto tight = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
    auto operation = an::ExecutionContext::create(*tight, {});
    auto input = an::FrameSource::create(*tight, sequenceDescription(), std::make_unique<SourceBackend>(control));
    ASSERT_TRUE(input.hasValue());
    EXPECT_EQ(input->read(*tight, *operation).code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(tight->memoryResource()->counters().liveBytes, 0u);
    EXPECT_EQ(input->state(), an::SequenceState::Failed);
}

TEST_F(SequenceTest, SinkCanBorrowSamplesAndRejectsForeignExecution)
{
    control->external = std::make_shared<std::array<std::byte, 24u>>();
    control->fault = 3;
    auto frame = makeFrame(engine, 0u, control.get());
    ASSERT_TRUE(frame.hasValue());
    auto output = sink(1u);
    auto foreign = an::EngineContext::create({.maxConcurrency = 1u});
    auto operation = an::ExecutionContext::create(*foreign, {});
    EXPECT_FALSE(output.append(engine, *operation, *frame).hasValue());
    EXPECT_FALSE(output.append(*foreign, execution, *frame).hasValue());
    EXPECT_FALSE(output.finish(*operation).hasValue());
    EXPECT_EQ(output.finish(an::ExecutionContext{}).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(control->appends, 0u);
    EXPECT_TRUE(output.append(engine, execution, *frame).hasValue());
    EXPECT_TRUE(output.finish(execution).hasValue());
    EXPECT_FALSE(output.finish(*operation).hasValue());
}

TEST_F(SequenceTest, SinkRefusesRegressingTimeAndOverflowingDurationWithoutWriting)
{
    auto output = sink();
    auto frame = makeFrame(engine, 0u);
    frame->position.presentationTicks = 40;
    ASSERT_TRUE(output.append(engine, execution, *frame).hasValue());
    frame->position.index = 1u;
    frame->position.presentationTicks = 20;
    EXPECT_FALSE(output.append(engine, execution, *frame).hasValue());
    frame->position.presentationTicks = 40;
    frame->position.durationTicks = (std::numeric_limits<std::int64_t>::max)();
    EXPECT_FALSE(output.append(engine, execution, *frame).hasValue());
    EXPECT_EQ(control->appends, 1u);
    EXPECT_EQ(output.state(), an::SequenceState::Open);
}

TEST(SequenceErrorTest, RefinementDoesNotParseTextAndRejectsUnknownFields)
{
    for (std::uint64_t reason = 0u; reason <= static_cast<std::uint64_t>(an::SequenceFailure::EncoderFailure); ++reason)
    {
        const auto value = static_cast<an::SequenceFailure>(reason);
        EXPECT_EQ(an::sequenceFailureReason(an::sequenceFailure(an::ErrorCode::InvalidData, value)), value);
    }
    EXPECT_FALSE(an::sequenceFailureReason({}));
    EXPECT_FALSE(an::sequenceFailureReason(an::Status::failure(an::ErrorCode::InvalidData)));
    const auto unknown =
        an::Status::failure(an::ErrorCode::InvalidData,
                            {.domain = an::ErrorDomain::Io,
                             .context = {an::StatusContextField::ofCount(an::ContextKey::SequenceReason, 999u)}});
    EXPECT_FALSE(an::sequenceFailureReason(unknown));
    const auto pastLast = an::Status::failure(
        an::ErrorCode::InvalidData,
        {.domain = an::ErrorDomain::Io,
         .context = {an::StatusContextField::ofCount(
             an::ContextKey::SequenceReason, static_cast<std::uint64_t>(an::SequenceFailure::EncoderFailure) + 1u)}});
    EXPECT_FALSE(an::sequenceFailureReason(pastLast));
}

TEST_F(SequenceTest, LargestKnownCountIsAnEofCursorAndUnknownMaximumIsRefused)
{
    auto descriptor = sequenceDescription();
    descriptor.frameCount = (std::numeric_limits<std::uint64_t>::max)();
    descriptor.durationTicks.reset();
    descriptor.timeBase.reset();
    descriptor.timestampsRequired = false;
    descriptor.seekByTime = false;
    control->fault = 11;
    auto input = source(descriptor);
    ASSERT_TRUE(input.seek(execution, {.index = *descriptor.frameCount}).hasValue());
    EXPECT_EQ(input.nextIndex(), *descriptor.frameCount);
    EXPECT_FALSE(input.read(engine, execution)->frame);
    EXPECT_EQ(control->reads, 0u);
    descriptor.frameCount.reset();
    auto unknown = source(descriptor);
    EXPECT_FALSE(unknown.seek(execution, {.index = (std::numeric_limits<std::uint64_t>::max)()}).hasValue());
    EXPECT_EQ(control->seeks, 1u);
}

TEST_F(SequenceTest, DescriptorIsCopiedAndOwnedFramesRetainTheirOwnContextLease)
{
    auto descriptor = sequenceDescription();
    auto input = source(descriptor);
    descriptor.frameCount = 0u;
    descriptor.nominalFrameRate->numerator = 1u;
    EXPECT_EQ(input.descriptor()->frameCount, 3u);
    EXPECT_EQ(input.descriptor()->nominalFrameRate->numerator, 25u);
    auto frame = input.read(engine, execution);
    ASSERT_TRUE(frame.hasValue());
    ASSERT_TRUE(frame->frame);
    const auto* bytes = frame->frame->frame.bytes().data();
    ASSERT_TRUE(input.close().ok());
    EXPECT_EQ(frame->frame->frame.bytes().data(), bytes);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 18u);
    execution = {};
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    frame->frame.reset();
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST_F(SequenceTest, SeekCannotClaimAFrameAtTimelineEndOrReturnEofForAResolvedTime)
{
    control->fault = 12;
    auto input = source();
    EXPECT_FALSE(input.seek(execution, {.kind = an::SequenceSeekKind::TimeAtOrAfter}).hasValue());
    EXPECT_EQ(input.state(), an::SequenceState::Failed);
    control->fault = 0;
    control->available = 0u;
    auto descriptor = sequenceDescription();
    descriptor.frameCount.reset();
    auto unknown = source(descriptor);
    ASSERT_TRUE(unknown.seek(execution, {.kind = an::SequenceSeekKind::TimeAtOrAfter}).hasValue());
    EXPECT_EQ(an::sequenceFailureReason(unknown.read(engine, execution).status()), an::SequenceFailure::UnexpectedEnd);
    EXPECT_EQ(unknown.state(), an::SequenceState::Failed);
}

TEST(SequenceLifecycleTest, EmptyHandlesAreClosed)
{
    an::FrameSource source;
    an::FrameSink sink;
    EXPECT_EQ(source.state(), an::SequenceState::Closed);
    EXPECT_EQ(sink.state(), an::SequenceState::Closed);
    EXPECT_TRUE(source.close().ok());
    EXPECT_TRUE(sink.close().ok());
}
} // namespace

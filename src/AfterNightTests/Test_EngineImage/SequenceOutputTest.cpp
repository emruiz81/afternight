// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/SequenceOutput.h>

#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
class SequenceOutputTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory =
            std::filesystem::temp_directory_path() /
            ("an-sequence-output-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        engine = std::move(*an::EngineContext::create({.maxConcurrency = 2u}));
        execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 1u}));
    }

    void TearDown() override
    {
        execution = {};
        EXPECT_TRUE(engine.shutdown().ok());
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(directory, error);
        ASSERT_FALSE(error);
        ASSERT_EQ(root.parent_path(), std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()));
        std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error) << error.message();
        EXPECT_FALSE(std::filesystem::exists(root));
    }

    static std::string utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    static std::filesystem::path native(std::string_view path)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    }

    static void fill(std::string_view path, std::size_t bytes, char value = 'x')
    {
        std::ofstream stream(native(path), std::ios::binary | std::ios::trunc);
        stream << std::string(bytes, value);
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

std::string parameterOf(const an::Status& status)
{
    const auto* field = status.findContext(an::ContextKey::Parameter);
    return field != nullptr ? field->text : std::string{};
}

TEST_F(SequenceOutputTest, PublishesAFinishedPayloadAtomicallyAndReportsIt)
{
    const auto destination = utf8(directory / u8"星-recording.ser");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    EXPECT_TRUE(output->valid());
    EXPECT_FALSE(output->committed());
    EXPECT_EQ(output->destinationPath(), destination);
    EXPECT_EQ(output->engineContextId(), engine.id());
    const std::string pending(output->pendingPath());
    EXPECT_TRUE(an::isPendingOutputPath(pending));
    EXPECT_EQ(pending.rfind(destination, 0u), 0u);
    ASSERT_TRUE(std::filesystem::exists(native(pending)));
    EXPECT_EQ(std::filesystem::file_size(native(pending)), 0u);
    EXPECT_FALSE(std::filesystem::exists(native(destination)));

    fill(pending, 1000u);
    auto report = output->commit(execution, {.framesWritten = 3u, .bytesWritten = 1000u});
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->bytesWritten, 1000u);
    EXPECT_TRUE(report->flushed);
    EXPECT_FALSE(report->replacedExisting);
    EXPECT_FALSE(output->valid());
    EXPECT_TRUE(output->committed());
    EXPECT_EQ(output->engineContextId(), 0u);
    EXPECT_EQ(output->pendingPath(), pending);
    EXPECT_FALSE(std::filesystem::exists(native(pending)));
    ASSERT_TRUE(std::filesystem::exists(native(destination)));
    EXPECT_EQ(std::filesystem::file_size(native(destination)), 1000u);
    output->abort();
    EXPECT_TRUE(std::filesystem::exists(native(destination)));
}

TEST_F(SequenceOutputTest, RefusesAPayloadThatDoesNotHoldTheReportedBytes)
{
    const auto destination = utf8(directory / "short.ser");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue());
    const std::string pending(output->pendingPath());
    fill(pending, 10u);
    auto report = output->commit(execution, {.framesWritten = 1u, .bytesWritten = 11u});
    ASSERT_FALSE(report.hasValue());
    EXPECT_EQ(report.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(report.status()), an::CodecFailure::ShortWrite);
    ASSERT_NE(report.status().findContext(an::ContextKey::Requested), nullptr);
    ASSERT_NE(report.status().findContext(an::ContextKey::Available), nullptr);
    EXPECT_EQ(report.status().findContext(an::ContextKey::Requested)->count, 11u);
    EXPECT_EQ(report.status().findContext(an::ContextKey::Available)->count, 10u);
    EXPECT_FALSE(output->valid());
    EXPECT_FALSE(output->committed());
    EXPECT_FALSE(std::filesystem::exists(native(pending)));
    EXPECT_FALSE(std::filesystem::exists(native(destination)));

    auto empty = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(empty.hasValue());
    auto nothing = empty->commit(execution, {});
    EXPECT_EQ(nothing.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(nothing.status()), an::CodecFailure::ShortWrite);
    EXPECT_FALSE(std::filesystem::exists(native(destination)));
}

TEST_F(SequenceOutputTest, ReplacePoliciesPendingConflictsAndMissingDirectoriesAreNamed)
{
    const auto destination = utf8(directory / "existing.ser");
    fill(destination, 5u, 'o');
    auto refused = an::SequenceOutput::begin(
        engine, execution, {.destinationPath = destination, .replacePolicy = an::OutputReplacePolicy::RefuseExisting});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::ExistingDestination);

    auto output = an::SequenceOutput::begin(
        engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    auto conflict = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_FALSE(conflict.hasValue());
    EXPECT_EQ(conflict.status().code(), an::ErrorCode::Busy);
    EXPECT_EQ(an::codecFailureReason(conflict.status()), an::CodecFailure::PendingConflict);
    EXPECT_TRUE(output->valid());
    fill(output->pendingPath(), 7u, 'n');
    auto report = output->commit(execution, {.framesWritten = 1u, .bytesWritten = 7u});
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_FALSE(report->flushed);
    EXPECT_TRUE(report->replacedExisting);
    EXPECT_EQ(std::filesystem::file_size(native(destination)), 7u);

    auto missing =
        an::SequenceOutput::begin(engine, execution, {.destinationPath = utf8(directory / "absent" / "frames.ser")});
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(missing.status()), an::CodecFailure::DestinationUnavailable);
    EXPECT_NE(missing.status().findContext(an::ContextKey::LibraryCode), nullptr);
}

TEST_F(SequenceOutputTest, AbortDestructionAndMovesRemoveOnlyTheirOwnPayload)
{
    const auto first = utf8(directory / "first.ser");
    const auto second = utf8(directory / "second.ser");
    std::string scopedPending;
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = first});
        ASSERT_TRUE(output.hasValue());
        scopedPending = output->pendingPath();
        fill(scopedPending, 3u);
    }
    EXPECT_FALSE(std::filesystem::exists(native(scopedPending)));

    auto a = an::SequenceOutput::begin(engine, execution, {.destinationPath = first});
    auto b = an::SequenceOutput::begin(engine, execution, {.destinationPath = second});
    ASSERT_TRUE(a.hasValue());
    ASSERT_TRUE(b.hasValue());
    const std::string aPending(a->pendingPath());
    const std::string bPending(b->pendingPath());
    an::SequenceOutput moved = std::move(*a);
    EXPECT_FALSE(a->valid());
    EXPECT_TRUE(a->pendingPath().empty());
    EXPECT_EQ(a->engineContextId(), 0u);
    EXPECT_TRUE(moved.valid());
    EXPECT_TRUE(std::filesystem::exists(native(aPending)));
    moved = std::move(*b);
    EXPECT_FALSE(std::filesystem::exists(native(aPending)));
    EXPECT_TRUE(std::filesystem::exists(native(bPending)));
    EXPECT_EQ(moved.pendingPath(), bPending);
    moved.abort();
    moved.abort();
    EXPECT_FALSE(moved.valid());
    EXPECT_FALSE(std::filesystem::exists(native(bPending)));
    EXPECT_FALSE(std::filesystem::exists(native(first)));
    EXPECT_FALSE(std::filesystem::exists(native(second)));
    EXPECT_EQ(moved.commit(execution, {.framesWritten = 1u, .bytesWritten = 3u}).status().code(),
              an::ErrorCode::InvalidState);
    EXPECT_EQ(an::SequenceOutput{}.commit(execution, {.framesWritten = 1u, .bytesWritten = 3u}).status().code(),
              an::ErrorCode::InvalidState);
}

TEST_F(SequenceOutputTest, ArgumentsContextsAndCancellationAreCheckedBeforeReserving)
{
    const auto destination = utf8(directory / "argument.ser");
    EXPECT_EQ(parameterOf(an::SequenceOutput::begin(engine, execution, {}).status()), "destinationPath");
    EXPECT_EQ(
        parameterOf(an::SequenceOutput::begin(engine, execution, {.destinationPath = std::string_view("bad\0path", 8u)})
                        .status()),
        "destinationPath");
    EXPECT_EQ(parameterOf(an::SequenceOutput::begin(
                              engine, execution,
                              {.destinationPath = destination, .durability = static_cast<an::OutputDurability>(9)})
                              .status()),
              "durability");
    EXPECT_EQ(parameterOf(an::SequenceOutput::begin(engine, execution,
                                                    {.destinationPath = destination,
                                                     .replacePolicy = static_cast<an::OutputReplacePolicy>(9)})
                              .status()),
              "replacePolicy");
    EXPECT_EQ(
        an::SequenceOutput::begin(an::EngineContext{}, execution, {.destinationPath = destination}).status().code(),
        an::ErrorCode::NotInitialized);

    auto other = an::EngineContext::create({.maxConcurrency = 1u});
    ASSERT_TRUE(other.hasValue());
    {
        auto foreign = an::ExecutionContext::create(*other, {});
        ASSERT_TRUE(foreign.hasValue());
        EXPECT_EQ(an::SequenceOutput::begin(engine, *foreign, {.destinationPath = destination}).status().code(),
                  an::ErrorCode::IncompatibleArguments);

        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
        ASSERT_TRUE(output.hasValue());
        fill(output->pendingPath(), 4u);
        auto mismatched = output->commit(*foreign, {.framesWritten = 1u, .bytesWritten = 4u});
        EXPECT_EQ(mismatched.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_TRUE(output->valid());
        EXPECT_TRUE(std::filesystem::exists(native(output->pendingPath())));

        std::stop_source stop;
        auto cancelled = an::ExecutionContext::create(engine, {.stopToken = stop.get_token()});
        ASSERT_TRUE(cancelled.hasValue());
        stop.request_stop();
        auto stopped = output->commit(*cancelled, {.framesWritten = 1u, .bytesWritten = 4u});
        EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
        EXPECT_FALSE(output->valid());
        EXPECT_FALSE(std::filesystem::exists(native(destination)));
        EXPECT_EQ(an::SequenceOutput::begin(engine, *cancelled, {.destinationPath = destination}).status().code(),
                  an::ErrorCode::Cancelled);
        EXPECT_TRUE(std::filesystem::is_empty(directory));
    }
    EXPECT_TRUE(other->shutdown().ok());
}

TEST_F(SequenceOutputTest, APendingPayloadHoldsItsEngineContextOpen)
{
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = utf8(directory / "held.ser")});
    ASSERT_TRUE(output.hasValue());
    execution = {};
    EXPECT_TRUE(engine.shutdown().failed());
    output->abort();
    execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 1u}));
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(SequenceOutputTest, AWriterClaimHoldsCommitAndHandsThePayloadBack)
{
    const auto destination = utf8(directory / "claimed.ser");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const std::string pending(output->pendingPath());

    an::SequenceOutputClaim nothing;
    EXPECT_FALSE(nothing.held());
    EXPECT_TRUE(nothing.pendingPath().empty());
    nothing.complete();
    nothing.release();
    EXPECT_EQ(an::SequenceOutput{}.claimWriter().status().code(), an::ErrorCode::InvalidState);

    auto claim = output->claimWriter();
    ASSERT_TRUE(claim.hasValue()) << claim.status().message();
    EXPECT_TRUE(claim->held());
    EXPECT_EQ(claim->pendingPath(), pending);
    EXPECT_EQ(output->claimWriter().status().code(), an::ErrorCode::InvalidState);

    // A payload holding every reported byte is still refused while its writer may be extending it.
    fill(pending, 12u);
    auto waiting = output->commit(execution, {.framesWritten = 1u, .bytesWritten = 12u});
    ASSERT_FALSE(waiting.hasValue());
    EXPECT_EQ(waiting.status().code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(output->valid());
    EXPECT_TRUE(std::filesystem::exists(native(pending)));
    EXPECT_FALSE(std::filesystem::exists(native(destination)));

    // Released without completing, on any thread, the payload goes back to its output to be claimed again.
    an::SequenceOutputClaim moved = std::move(*claim);
    EXPECT_FALSE(claim->held());
    EXPECT_TRUE(moved.held());
    std::thread([&moved] { moved.release(); }).join();
    moved.release();
    EXPECT_FALSE(moved.held());
    EXPECT_TRUE(std::filesystem::exists(native(pending)));

    auto writer = output->claimWriter();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    writer->complete();
    EXPECT_EQ(output->claimWriter().status().code(), an::ErrorCode::InvalidState);
    auto report = output->commit(execution, {.framesWritten = 1u, .bytesWritten = 12u});
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(std::filesystem::file_size(native(destination)), 12u);
    writer->release();
    EXPECT_TRUE(std::filesystem::exists(native(destination)));
    EXPECT_EQ(output->claimWriter().status().code(), an::ErrorCode::InvalidState);
}

TEST_F(SequenceOutputTest, AnOutputEndedUnderAClaimLeavesRemovalToTheClaim)
{
    std::string abortedPending;
    std::string destroyedPending;
    an::SequenceOutputClaim abortedClaim;
    an::SequenceOutputClaim destroyedClaim;
    {
        auto aborted =
            an::SequenceOutput::begin(engine, execution, {.destinationPath = utf8(directory / "aborted.ser")});
        auto destroyed =
            an::SequenceOutput::begin(engine, execution, {.destinationPath = utf8(directory / "destroyed.ser")});
        ASSERT_TRUE(aborted.hasValue()) << aborted.status().message();
        ASSERT_TRUE(destroyed.hasValue()) << destroyed.status().message();
        abortedPending = aborted->pendingPath();
        destroyedPending = destroyed->pendingPath();
        auto first = aborted->claimWriter();
        auto second = destroyed->claimWriter();
        ASSERT_TRUE(first.hasValue()) << first.status().message();
        ASSERT_TRUE(second.hasValue()) << second.status().message();
        abortedClaim = std::move(*first);
        destroyedClaim = std::move(*second);
        fill(abortedPending, 5u);
        aborted->abort();
        EXPECT_FALSE(aborted->valid());
        EXPECT_EQ(aborted->commit(execution, {.framesWritten = 1u, .bytesWritten = 5u}).status().code(),
                  an::ErrorCode::InvalidState);
    }
    // Neither output removed a payload its writer may still hold open; each claim removes its own.
    EXPECT_TRUE(std::filesystem::exists(native(abortedPending)));
    EXPECT_TRUE(std::filesystem::exists(native(destroyedPending)));
    EXPECT_TRUE(abortedClaim.held());
    EXPECT_EQ(abortedClaim.pendingPath(), abortedPending);
    abortedClaim.complete();
    std::thread([&abortedClaim] { abortedClaim.release(); }).join();
    EXPECT_FALSE(abortedClaim.held());
    EXPECT_FALSE(std::filesystem::exists(native(abortedPending)));

    // Taking over another claim releases the one held first.
    destroyedClaim = an::SequenceOutputClaim{};
    EXPECT_FALSE(destroyedClaim.held());
    EXPECT_FALSE(std::filesystem::exists(native(destroyedPending)));
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}
} // namespace

#ifdef _WIN32
namespace
{
class PublicationTrace final : public an::TraceSink
{
public:
    std::ifstream* release = nullptr;
    std::stop_source* stop = nullptr;
    std::uint64_t retries = 0u;
    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (std::string_view(event.category) != "io.output" || std::string_view(event.name) != "publish-retry")
            return;
        ++retries;
        if (release != nullptr)
            release->close();
        if (stop != nullptr)
            stop->request_stop();
    }
};
} // namespace

TEST_F(SequenceOutputTest, PublicationSurvivesATemporaryWindowsSharingConflict)
{
    for (const auto durability : {an::OutputDurability::CloseOnly, an::OutputDurability::FlushBeforePublish})
        for (const bool holdPending : {false, true})
        {
            if (holdPending && durability == an::OutputDurability::FlushBeforePublish)
                continue; // A flush needs to open its own source handle before publication.
            const auto destination = utf8(directory / "transient.ser");
            fill(destination, 5u, 'o');
            auto output = an::SequenceOutput::begin(engine, execution,
                                                    {.destinationPath = destination, .durability = durability});
            ASSERT_TRUE(output.hasValue());
            const std::string pending(output->pendingPath());
            fill(pending, 7u, 'n');
            std::ifstream holder(native(holdPending ? pending : destination), std::ios::binary);
            ASSERT_TRUE(holder.is_open());
            PublicationTrace trace;
            trace.release = &holder;
            auto publishing = an::ExecutionContext::create(engine, {.traceSink = &trace, .maxConcurrency = 1u});
            ASSERT_TRUE(publishing.hasValue());
            const auto report = output->commit(*publishing, {.framesWritten = 1u, .bytesWritten = 7u});
            ASSERT_TRUE(report.hasValue()) << report.status().message();
            EXPECT_GE(trace.retries, 1u);
            EXPECT_TRUE(output->committed());
            EXPECT_EQ(std::filesystem::file_size(native(destination)), 7u);
            EXPECT_FALSE(std::filesystem::exists(native(pending)));
        }
}

TEST_F(SequenceOutputTest, PublicationWaitCanBeCancelledAndNeverReplacesTheDestination)
{
    const auto destination = utf8(directory / "cancel-retry.ser");
    fill(destination, 5u, 'o');
    auto output = an::SequenceOutput::begin(
        engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
    ASSERT_TRUE(output.hasValue());
    const std::string pending(output->pendingPath());
    fill(pending, 7u, 'n');
    std::ifstream holder(native(destination), std::ios::binary);
    ASSERT_TRUE(holder.is_open());
    std::stop_source stop;
    PublicationTrace trace;
    trace.stop = &stop;
    auto publishing = an::ExecutionContext::create(
        engine, {.stopToken = stop.get_token(), .traceSink = &trace, .maxConcurrency = 1u});
    ASSERT_TRUE(publishing.hasValue());
    const auto report = output->commit(*publishing, {.framesWritten = 1u, .bytesWritten = 7u});
    ASSERT_FALSE(report.hasValue());
    EXPECT_EQ(report.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(trace.retries, 1u);
    EXPECT_FALSE(output->committed());
    EXPECT_EQ(std::filesystem::file_size(native(destination)), 5u);
    EXPECT_FALSE(std::filesystem::exists(native(pending)));
}
#endif

#ifdef _WIN32
TEST_F(SequenceOutputTest, PublicationWaitHonoursADeadlineAndKeepsTheLastPlatformFailure)
{
    const auto destination = utf8(directory / "retry-deadline.ser");
    fill(destination, 5u, 'o');
    for (const bool deadline : {true, false})
    {
        auto output = an::SequenceOutput::begin(
            engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
        ASSERT_TRUE(output.hasValue());
        const std::string pending(output->pendingPath());
        fill(pending, 7u, 'n');
        std::ifstream holder(native(destination), std::ios::binary);
        ASSERT_TRUE(holder.is_open());
        PublicationTrace trace;
        an::ExecutionOptions options;
        options.maxConcurrency = 1u;
        options.traceSink = &trace;
        if (deadline)
            options.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        auto publishing = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(publishing.hasValue());
        const auto report = output->commit(*publishing, {.framesWritten = 1u, .bytesWritten = 7u});
        ASSERT_FALSE(report.hasValue());
        EXPECT_GE(trace.retries, 1u);
        if (deadline)
            EXPECT_EQ(report.status().code(), an::ErrorCode::DeadlineExceeded);
        else
        {
            EXPECT_EQ(an::codecFailureReason(report.status()), an::CodecFailure::PublishFailure);
            const auto* platform = report.status().findContext(an::ContextKey::LibraryCode);
            ASSERT_NE(platform, nullptr);
            EXPECT_NE(platform->number, 0);
        }
        EXPECT_FALSE(output->committed());
        EXPECT_EQ(std::filesystem::file_size(native(destination)), 5u);
        EXPECT_FALSE(std::filesystem::exists(native(pending)));
    }
}
#endif

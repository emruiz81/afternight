// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A request's whole journey, as a process dialog will make it: the GUI thread issues a ticket over a document, a
/// worker admits an execution under the ticket's stop token, reads the document under a lease and computes a new image,
/// and the terminal report brings it back to the GUI thread, which publishes it through the document's writer only when
/// the ticket still commits. A write made meanwhile, a newer request and a close each leave the document exactly as
/// the GUI thread last made it.

#include "ControlsSessionTestSupport.h"

#include "session/CommitTicket.h"
#include "session/ExecutionScope.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QObject>

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace
{

using an::consumer::AssetReadLease;
using an::consumer::EngineSession;
using an::consumer::ImageAsset;
using an::tests::controls::documentSampleAt;
using an::tests::controls::filledDocument;
using an::tests::controls::openProvider;
using an::tests::controls::pumpUntil;
using an::tests::controls::writeSample;
using an::ui::CommitTicket;
using an::ui::CommitVerdict;
using an::ui::ExecutionObserver;
using an::ui::ExecutionOutcome;
using an::ui::ExecutionRequest;
using an::ui::ExecutionScope;
using an::ui::ImageDocument;
using an::ui::ImageDocumentWriter;
using an::ui::RequestTracker;

/// What one request came to on the GUI thread.
struct Completion
{
    std::optional<CommitVerdict> verdict;
    an::Status status;
    bool published = false;
};

/// The worker's result, handed to the GUI thread through the terminal report.
struct ResultSlot
{
    std::mutex mutex;
    std::optional<ImageAsset> image;
};

/// Opens the gate a worker waits at before it finishes.
struct Gate
{
    std::promise<void> open;
    std::shared_future<void> opened = open.get_future().share();
};

/// The dialog's half: an observer that, on the GUI thread, asks the ticket and publishes the result through the
/// document's writer only when it commits.
[[nodiscard]] ExecutionObserver publishingObserver(QObject* dialog, ImageDocument& document, const CommitTicket& ticket,
                                                   const std::shared_ptr<ResultSlot>& slot,
                                                   const std::shared_ptr<Completion>& completion)
{
    ExecutionObserver observer;
    observer.receiver = dialog;
    observer.finished = [&document, ticket, slot, completion](const ExecutionOutcome& outcome) {
        completion->status = outcome.status;
        completion->verdict = ticket.verdict();
        if (*completion->verdict != CommitVerdict::Commit || !outcome.status.ok())
        {
            return;
        }
        std::optional<ImageAsset> image;
        {
            std::lock_guard lock(slot->mutex);
            image.swap(slot->image);
        }
        if (!image.has_value())
        {
            return;
        }
        an::Result<ImageDocumentWriter> writer = document.tryWrite();
        if (writer.hasValue() && writer->replace(std::move(*image)).ok())
        {
            completion->published = true;
        }
    };
    return observer;
}

/// The worker's half: admit under the ticket, read the document, compute "the value at (0, 0) plus `add`" as a new
/// image, wait at the gate while stopping is observed, and finish.
void runProcess(const EngineSession& session, const ImageDocument& document, const CommitTicket& ticket,
                ExecutionObserver observer, const std::shared_ptr<ResultSlot>& slot, float add,
                std::shared_future<void> gate)
{
    ExecutionRequest request;
    request.stopToken = ticket.stopToken();
    request.logFrom = std::nullopt;
    an::Result<ExecutionScope> scope = ExecutionScope::admit(session, request, std::move(observer));
    if (!scope.hasValue())
    {
        return;
    }
    an::Result<ImageAsset> result = an::Status::failure(an::ErrorCode::Cancelled);
    {
        an::Result<AssetReadLease> lease = document.read(ticket.stopToken());
        if (!lease.hasValue())
        {
            scope->finish(lease.status());
            return;
        }
        result = lease->snapshot();
    }
    if (!result.hasValue())
    {
        scope->finish(result.status());
        return;
    }
    {
        an::Result<an::consumer::AssetWriteLease> writer = result->write();
        if (!writer.hasValue())
        {
            scope->finish(writer.status());
            return;
        }
        const float value = an::tests::controls::sampleAt(writer->image(), 0u, 0u) + add;
        writeSample(*writer, 0u, 0u, value);
    }
    while (gate.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
    {
        const an::Status checkpoint = scope->execution().checkCancellation();
        if (!checkpoint.ok())
        {
            scope->finish(checkpoint);
            return;
        }
    }
    {
        std::lock_guard lock(slot->mutex);
        slot->image.emplace(std::move(*result));
    }
    scope->finish(an::Status::success());
}

} // namespace

TEST(CompletionFlowTest, AResultCommitsWhenNothingChangedMeanwhile)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    QObject dialog;
    RequestTracker tracker;
    an::Result<CommitTicket> ticket = tracker.issue(*document);
    ASSERT_TRUE(ticket.hasValue());
    const auto slot = std::make_shared<ResultSlot>();
    const auto completion = std::make_shared<Completion>();
    Gate gate;
    gate.open.set_value();
    std::thread worker(runProcess, std::cref(session), std::cref(*document), *ticket,
                       publishingObserver(&dialog, *document, *ticket, slot, completion), slot, 2.0f, gate.opened);
    ASSERT_TRUE(pumpUntil([&completion] { return completion->verdict.has_value(); }));
    worker.join();
    EXPECT_EQ(*completion->verdict, CommitVerdict::Commit);
    EXPECT_TRUE(completion->status.ok()) << completion->status.message();
    EXPECT_TRUE(completion->published);
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 3.0f);
}

TEST(CompletionFlowTest, AWriteMadeMeanwhileDiscardsTheResult)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    QObject dialog;
    RequestTracker tracker;
    an::Result<CommitTicket> ticket = tracker.issue(*document);
    ASSERT_TRUE(ticket.hasValue());
    const auto slot = std::make_shared<ResultSlot>();
    const auto completion = std::make_shared<Completion>();
    Gate gate;
    std::thread worker(runProcess, std::cref(session), std::cref(*document), *ticket,
                       publishingObserver(&dialog, *document, *ticket, slot, completion), slot, 2.0f, gate.opened);
    // The person paints on the image while the process runs.
    ASSERT_TRUE(an::tests::controls::pumpUntil([&document] {
        an::Result<ImageDocumentWriter> writer = document->tryWrite();
        if (!writer.hasValue())
        {
            return false;
        }
        writeSample(writer->asset(), 0u, 0u, 7.0f);
        return true;
    }));
    const an::ui::DocumentRevision painted = document->revision();
    gate.open.set_value();
    ASSERT_TRUE(pumpUntil([&completion] { return completion->verdict.has_value(); }));
    worker.join();
    EXPECT_EQ(*completion->verdict, CommitVerdict::SourceChanged);
    EXPECT_TRUE(completion->status.ok()) << "the work itself succeeded; only its result is stale";
    EXPECT_FALSE(completion->published);
    EXPECT_EQ(document->revision(), painted) << "a stale result changed the document";
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 7.0f);
}

TEST(CompletionFlowTest, ANewerRequestStopsTheOlderAndOnlyTheNewestCommits)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    QObject dialog;
    RequestTracker tracker;

    an::Result<CommitTicket> older = tracker.issue(*document);
    ASSERT_TRUE(older.hasValue());
    const auto olderSlot = std::make_shared<ResultSlot>();
    const auto olderCompletion = std::make_shared<Completion>();
    Gate never;
    std::thread olderWorker(runProcess, std::cref(session), std::cref(*document), *older,
                            publishingObserver(&dialog, *document, *older, olderSlot, olderCompletion), olderSlot,
                            10.0f, never.opened);
    // The person changes a parameter once the first request is running; one stopped before it was admitted would
    // never report at all.
    ASSERT_TRUE(pumpUntil([&session] { return session.outstanding().operations == 1u; }));

    an::Result<CommitTicket> newer = tracker.issue(*document);
    ASSERT_TRUE(newer.hasValue());
    EXPECT_TRUE(older->stopToken().stop_requested());
    const auto newerSlot = std::make_shared<ResultSlot>();
    const auto newerCompletion = std::make_shared<Completion>();
    Gate gate;
    gate.open.set_value();
    std::thread newerWorker(runProcess, std::cref(session), std::cref(*document), *newer,
                            publishingObserver(&dialog, *document, *newer, newerSlot, newerCompletion), newerSlot, 2.0f,
                            gate.opened);

    ASSERT_TRUE(pumpUntil([&olderCompletion, &newerCompletion] {
        return olderCompletion->verdict.has_value() && newerCompletion->verdict.has_value();
    }));
    olderWorker.join();
    newerWorker.join();
    never.open.set_value();
    EXPECT_EQ(*olderCompletion->verdict, CommitVerdict::Superseded);
    EXPECT_FALSE(olderCompletion->published);
    EXPECT_EQ(*newerCompletion->verdict, CommitVerdict::Commit);
    EXPECT_TRUE(newerCompletion->published);
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 3.0f) << "the document holds another result than the newest";
}

TEST(CompletionFlowTest, ClosingTheDocumentDiscardsTheResultAndTheWorkDrains)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    QObject dialog;
    RequestTracker tracker;
    an::Result<CommitTicket> ticket = tracker.issue(*document);
    ASSERT_TRUE(ticket.hasValue());
    const auto slot = std::make_shared<ResultSlot>();
    const auto completion = std::make_shared<Completion>();
    Gate gate;
    std::thread worker(runProcess, std::cref(session), std::cref(*document), *ticket,
                       publishingObserver(&dialog, *document, *ticket, slot, completion), slot, 2.0f, gate.opened);
    const an::ui::DocumentRevision before = document->revision();
    document->close();
    gate.open.set_value();
    ASSERT_TRUE(pumpUntil([&completion] { return completion->verdict.has_value(); }));
    worker.join();
    EXPECT_EQ(*completion->verdict, CommitVerdict::DocumentClosed);
    EXPECT_FALSE(completion->published);
    EXPECT_EQ(document->revision(), before);
    EXPECT_EQ(session.outstanding().operations, 0u) << "the work did not drain";
}

TEST(CompletionFlowTest, ADialogClosedDuringTheWorkHearsNothing)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    auto dialog = std::make_unique<QObject>();
    RequestTracker tracker;
    an::Result<CommitTicket> ticket = tracker.issue(*document);
    ASSERT_TRUE(ticket.hasValue());
    const auto slot = std::make_shared<ResultSlot>();
    const auto completion = std::make_shared<Completion>();
    Gate gate;
    std::thread worker(runProcess, std::cref(session), std::cref(*document), *ticket,
                       publishingObserver(dialog.get(), *document, *ticket, slot, completion), slot, 2.0f, gate.opened);
    ASSERT_TRUE(pumpUntil([&session] { return session.outstanding().operations == 1u; }));
    dialog.reset();
    tracker.cancel();
    gate.open.set_value();
    worker.join();
    an::tests::controls::pumpQuietly();
    EXPECT_FALSE(completion->verdict.has_value()) << "a destroyed dialog's completion ran";
    EXPECT_EQ(documentSampleAt(*document, 0u, 0u), 1.0f);
    EXPECT_EQ(session.outstanding().operations, 0u);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Commit tickets: a request's result is published only while every identity it captured still holds, it is the
/// current request of its tracker and it was not cancelled. Cancelling refuses a completion already on its way, a new
/// request supersedes and stops the one before, closing a document refuses every ticket against it before its work
/// drains, and a ticket keeps no image alive.

#include "ControlsSessionTestSupport.h"

#include "session/CommitTicket.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace
{

using an::consumer::ImageAsset;
using an::tests::controls::filledAsset;
using an::tests::controls::filledDocument;
using an::tests::controls::openProvider;
using an::tests::controls::pumpUntil;
using an::tests::controls::writeSample;
using an::ui::CommitTicket;
using an::ui::CommitVerdict;
using an::ui::ImageDocument;
using an::ui::ImageDocumentWriter;
using an::ui::RequestTracker;

/// A ticket issued over a document with no secondary input; fails the case when it is refused.
[[nodiscard]] CommitTicket issued(RequestTracker& tracker, const ImageDocument& source)
{
    an::Result<CommitTicket> ticket = tracker.issue(source);
    EXPECT_TRUE(ticket.hasValue()) << ticket.status().message();
    return ticket.hasValue() ? *ticket : CommitTicket{};
}

} // namespace

TEST(CommitTicketTest, AnUnchangedCurrentRequestCommits)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    RequestTracker tracker;
    EXPECT_EQ(tracker.currentRequest(), 0u);
    const CommitTicket ticket = issued(tracker, *document);
    EXPECT_TRUE(ticket.valid());
    EXPECT_EQ(ticket.requestId(), 1u);
    EXPECT_EQ(tracker.currentRequest(), 1u);
    EXPECT_EQ(ticket.source(), document->revision());
    EXPECT_TRUE(ticket.secondary().empty());
    EXPECT_FALSE(ticket.stopToken().stop_requested());
    {
        an::Result<an::consumer::AssetReadLease> lease = document->read();
        ASSERT_TRUE(lease.hasValue());
    }
    EXPECT_EQ(ticket.verdict(), CommitVerdict::Commit);
    const CommitTicket copy = ticket;
    EXPECT_EQ(copy.verdict(), CommitVerdict::Commit);
    EXPECT_EQ(copy.requestId(), ticket.requestId());
}

TEST(CommitTicketTest, AnEmptyTicketNeverCommits)
{
    const CommitTicket empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.requestId(), 0u);
    EXPECT_EQ(empty.verdict(), CommitVerdict::Empty);
    EXPECT_STREQ(an::ui::commitVerdictName(CommitVerdict::Empty), "empty");
    EXPECT_STREQ(an::ui::commitVerdictName(CommitVerdict::Commit), "commit");
    EXPECT_STREQ(an::ui::commitVerdictName(static_cast<CommitVerdict>(200)), "unknown");
}

TEST(CommitTicketTest, ANewRequestSupersedesAndStopsTheOneBefore)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    RequestTracker tracker;
    const CommitTicket first = issued(tracker, *document);
    const CommitTicket second = issued(tracker, *document);
    EXPECT_EQ(second.requestId(), first.requestId() + 1u);
    EXPECT_EQ(tracker.currentRequest(), second.requestId());
    EXPECT_TRUE(first.stopToken().stop_requested()) << "a superseded request's work was left running";
    EXPECT_FALSE(second.stopToken().stop_requested());
    EXPECT_EQ(first.verdict(), CommitVerdict::Superseded);
    EXPECT_EQ(second.verdict(), CommitVerdict::Commit);

    RequestTracker other;
    const CommitTicket elsewhere = issued(other, *document);
    EXPECT_EQ(elsewhere.verdict(), CommitVerdict::Commit) << "one dialog's request superseded another's";
    EXPECT_EQ(second.verdict(), CommitVerdict::Commit);
}

TEST(CommitTicketTest, CancellingRefusesACompletionAlreadyQueued)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    RequestTracker tracker;
    const CommitTicket ticket = issued(tracker, *document);

    QObject dialog;
    std::optional<CommitVerdict> decided;
    std::thread worker([ticket, &dialog, &decided] {
        // The worker finishes and queues its completion before the cancel is made.
        QMetaObject::invokeMethod(&dialog, [ticket, &decided] { decided = ticket.verdict(); }, Qt::QueuedConnection);
    });
    worker.join();
    tracker.cancel();
    EXPECT_EQ(tracker.currentRequest(), 0u);
    EXPECT_TRUE(ticket.stopToken().stop_requested());
    ASSERT_TRUE(pumpUntil([&decided] { return decided.has_value(); }));
    EXPECT_EQ(*decided, CommitVerdict::Cancelled);
}

TEST(CommitTicketTest, DestroyingTheTrackerCancelsItsCurrentRequest)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    CommitTicket ticket;
    {
        RequestTracker tracker;
        ticket = issued(tracker, *document);
    }
    EXPECT_TRUE(ticket.stopToken().stop_requested());
    EXPECT_EQ(ticket.verdict(), CommitVerdict::Cancelled);
}

TEST(CommitTicketTest, AWriteATransitionOrAReplacementOfTheSourceMakesTheResultStale)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    RequestTracker written;
    RequestTracker transitioned;
    RequestTracker replaced;

    const CommitTicket beforeWrite = issued(written, *document);
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(writer->asset(), 0u, 0u, 2.0f);
    }
    EXPECT_EQ(beforeWrite.verdict(), CommitVerdict::SourceChanged);

    const CommitTicket beforeTransition = issued(transitioned, *document);
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer->asset().transition(an::tests::controls::linearTransition()).ok());
    }
    EXPECT_EQ(beforeTransition.verdict(), CommitVerdict::SourceChanged);

    const CommitTicket beforeReplacement = issued(replaced, *document);
    std::optional<ImageAsset> replacement = filledAsset(session, 1.0f);
    ASSERT_TRUE(replacement.has_value());
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer->replace(std::move(*replacement)).ok());
    }
    EXPECT_EQ(beforeReplacement.verdict(), CommitVerdict::SourceChanged);
}

TEST(CommitTicketTest, AChangedSecondaryInputMakesTheResultStale)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> source = filledDocument(session, 1.0f);
    std::optional<ImageDocument> mask = filledDocument(session, 1.0f);
    std::optional<ImageDocument> reference = filledDocument(session, 1.0f);
    ASSERT_TRUE(source.has_value());
    ASSERT_TRUE(mask.has_value());
    ASSERT_TRUE(reference.has_value());
    RequestTracker tracker;
    const std::array<const ImageDocument*, 2u> secondary{&*mask, &*reference};
    an::Result<CommitTicket> ticket = tracker.issue(*source, secondary);
    ASSERT_TRUE(ticket.hasValue()) << ticket.status().message();
    ASSERT_EQ(ticket->secondary().size(), 2u);
    EXPECT_EQ(ticket->secondary()[0], mask->revision());
    EXPECT_EQ(ticket->secondary()[1], reference->revision());
    EXPECT_EQ(ticket->verdict(), CommitVerdict::Commit);
    {
        an::Result<ImageDocumentWriter> writer = reference->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(writer->asset(), 1u, 1u, 3.0f);
    }
    EXPECT_EQ(ticket->verdict(), CommitVerdict::SecondaryChanged);
}

TEST(CommitTicketTest, ClosingADocumentRefusesItsTicketsBeforeItsWorkDrains)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<ImageDocument> source = filledDocument(session, 1.0f);
    std::optional<ImageDocument> mask = filledDocument(session, 1.0f);
    ASSERT_TRUE(source.has_value());
    ASSERT_TRUE(mask.has_value());
    RequestTracker sourceTracker;
    RequestTracker maskTracker;
    const CommitTicket overSource = issued(sourceTracker, *source);
    const std::array<const ImageDocument*, 1u> secondary{&*mask};
    an::Result<CommitTicket> overMask = maskTracker.issue(*source, secondary);
    ASSERT_TRUE(overMask.hasValue());

    an::Result<an::consumer::AssetReadLease> stillReading = source->read();
    ASSERT_TRUE(stillReading.hasValue());
    source->close();
    EXPECT_EQ(overSource.verdict(), CommitVerdict::DocumentClosed)
        << "a ticket was still good while the work against the document had yet to drain";
    stillReading->release();

    mask->close();
    EXPECT_EQ(overMask->verdict(), CommitVerdict::DocumentClosed);

    an::Result<CommitTicket> refused = sourceTracker.issue(*source);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(sourceTracker.currentRequest(), overSource.requestId()) << "a refused request superseded the current one";
    EXPECT_EQ(sourceTracker.issue(ImageDocument{}).status().code(), an::ErrorCode::NotInitialized);
}

TEST(CommitTicketTest, ATicketKeepsNoImageAlive)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    RequestTracker tracker;
    CommitTicket ticket;
    std::weak_ptr<const ImageAsset> asset;
    {
        std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
        ASSERT_TRUE(document.has_value());
        asset = document->asset();
        ticket = issued(tracker, *document);
    }
    EXPECT_TRUE(asset.expired()) << "a ticket kept the document's image alive";
    EXPECT_EQ(provider->engineSession().outstanding().assets, 0u);
    EXPECT_EQ(ticket.verdict(), CommitVerdict::DocumentClosed) << "a destroyed document's ticket must not commit";
}

TEST(CommitTicketTest, TheVerdictOrderIsStated)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    RequestTracker tracker;
    const CommitTicket ticket = issued(tracker, *document);
    {
        an::Result<ImageDocumentWriter> writer = document->write();
        ASSERT_TRUE(writer.hasValue());
        writeSample(writer->asset(), 0u, 0u, 2.0f);
    }
    document->close();
    EXPECT_EQ(ticket.verdict(), CommitVerdict::DocumentClosed) << "closed comes before changed";
    tracker.cancel();
    EXPECT_EQ(ticket.verdict(), CommitVerdict::Cancelled) << "cancelled comes before everything";
}

// A request that reads no document - a file read that will make one - has no revision to compare: it commits while it
// is current and uncancelled, a newer request of either kind supersedes it, and it supersedes a bound one.
TEST(CommitTicketTest, AnUnboundRequestCommitsWhileItIsCurrent)
{
    RequestTracker tracker;
    const CommitTicket first = tracker.issueUnbound();
    EXPECT_TRUE(first.valid());
    EXPECT_EQ(first.requestId(), 1u);
    EXPECT_EQ(tracker.currentRequest(), 1u);
    EXPECT_EQ(first.source().documentIdentity, 0u) << "an unbound request names no document";
    EXPECT_TRUE(first.secondary().empty());
    EXPECT_FALSE(first.stopToken().stop_requested());
    EXPECT_EQ(first.verdict(), CommitVerdict::Commit);

    const CommitTicket second = tracker.issueUnbound();
    EXPECT_EQ(first.verdict(), CommitVerdict::Superseded);
    EXPECT_TRUE(first.stopToken().stop_requested());
    EXPECT_EQ(second.verdict(), CommitVerdict::Commit);

    tracker.cancel();
    EXPECT_EQ(second.verdict(), CommitVerdict::Cancelled);
    EXPECT_TRUE(second.stopToken().stop_requested());
}

TEST(CommitTicketTest, BoundAndUnboundRequestsOfOneTrackerSupersedeEachOther)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    RequestTracker tracker;
    const CommitTicket bound = issued(tracker, *document);
    const CommitTicket unbound = tracker.issueUnbound();
    EXPECT_EQ(bound.verdict(), CommitVerdict::Superseded);
    EXPECT_EQ(unbound.verdict(), CommitVerdict::Commit);
    const CommitTicket boundAgain = issued(tracker, *document);
    EXPECT_EQ(unbound.verdict(), CommitVerdict::Superseded);
    EXPECT_EQ(boundAgain.verdict(), CommitVerdict::Commit);
}

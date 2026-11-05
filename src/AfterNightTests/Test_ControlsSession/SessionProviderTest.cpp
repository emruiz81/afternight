// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The session provider: everything Controls builds runs in the one session the provider lends. Documents, writers,
/// replacements, snapshots, tickets and execution scopes build no engine context of their own, count against the
/// provider's session while they live, and leave it free to shut down once they are gone.

#include "ControlsSessionTestSupport.h"

#include "session/CommitTicket.h"
#include "session/DerivedResultKey.h"
#include "session/ExecutionScope.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QObject>

#include <gtest/gtest.h>

#include <optional>
#include <thread>

namespace
{

using an::consumer::EngineSession;
using an::consumer::ImageAsset;
using an::tests::controls::filledAsset;
using an::tests::controls::filledDocument;
using an::tests::controls::openProvider;
using an::tests::controls::pumpQuietly;
using an::ui::CommitTicket;
using an::ui::ExecutionRequest;
using an::ui::ExecutionScope;
using an::ui::IEngineSessionProvider;
using an::ui::ImageDocument;
using an::ui::ImageDocumentWriter;
using an::ui::RequestTracker;

/// Everything the bridge offers, run once against the provider's session.
void exerciseTheBridge(const IEngineSessionProvider& provider)
{
    const EngineSession& session = provider.engineSession();
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    RequestTracker tracker;
    an::Result<CommitTicket> ticket = tracker.issue(*document);
    ASSERT_TRUE(ticket.hasValue());

    ExecutionRequest request;
    request.stopToken = ticket->stopToken();
    request.logFrom = std::nullopt;
    an::Result<ExecutionScope> scope = ExecutionScope::admit(session, request, {});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    EXPECT_EQ(scope->engine().id(), session.contextId()) << "the execution ran in another context";
    an::Result<an::consumer::AssetReadLease> lease = document->read(ticket->stopToken());
    ASSERT_TRUE(lease.hasValue());
    an::Result<ImageAsset> result = lease->snapshot();
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    lease->release();
    scope->finish(an::Status::success());

    EXPECT_EQ(ticket->verdict(), an::ui::CommitVerdict::Commit);
    an::Result<ImageDocumentWriter> writer = document->tryWrite();
    ASSERT_TRUE(writer.hasValue());
    ASSERT_TRUE(writer->replace(std::move(*result)).ok());
    writer->release();
    const an::ui::DerivedResultKey key = an::ui::derivedResultKey(document->revision());
    EXPECT_EQ(key.documentIdentity, document->identity());
    document->close();
}

} // namespace

TEST(SessionProviderTest, ControlsBuildsNoContextOfItsOwn)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const std::uint64_t before = provider->engineSession().contextId();
    const std::uint64_t generation = provider->engineSession().generation();
    exerciseTheBridge(*provider);
    pumpQuietly();
    EXPECT_EQ(provider->engineSession().generation(), generation) << "the session was rebuilt";
    EXPECT_EQ(provider->engineSession().contextId(), before);

    // Context identities are drawn from one counter, so the next context built has the next one exactly when no
    // other context was built in between.
    std::optional<an::tests::controls::TestSessionProvider> probe = openProvider(1u);
    ASSERT_TRUE(probe.has_value());
    EXPECT_EQ(probe->engineSession().contextId(), before + 1u) << "something Controls did built an engine context";
}

TEST(SessionProviderTest, WhatControlsHoldsCountsAgainstTheSessionUntilItIsGone)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const EngineSession& session = provider->engineSession();
    {
        std::optional<ImageDocument> document = filledDocument(session, 1.0f);
        ASSERT_TRUE(document.has_value());
        EXPECT_EQ(session.outstanding().assets, 1u);
        an::Result<ExecutionScope> scope = ExecutionScope::tryAdmit(session, ExecutionRequest{}, {});
        ASSERT_TRUE(scope.hasValue());
        EXPECT_EQ(session.outstanding().operations, 1u);
        const an::Status refused = provider->owned().shutdown();
        EXPECT_EQ(refused.code(), an::ErrorCode::Busy) << "the session shut down under a document and a scope";
    }
    pumpQuietly();
    EXPECT_TRUE(session.outstanding().none());
    const an::Status shut = provider->owned().shutdown();
    EXPECT_TRUE(shut.ok()) << shut.message();
}

TEST(SessionProviderTest, AWriterOutlivingItsDocumentStillHoldsTheSession)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    std::optional<ImageDocumentWriter> writer;
    {
        std::optional<ImageDocument> document = filledDocument(provider->engineSession(), 1.0f);
        ASSERT_TRUE(document.has_value());
        an::Result<ImageDocumentWriter> taken = document->write();
        ASSERT_TRUE(taken.hasValue());
        writer.emplace(std::move(*taken));
    }
    EXPECT_EQ(provider->engineSession().outstanding().assets, 1u);
    EXPECT_EQ(provider->owned().shutdown().code(), an::ErrorCode::Busy);
    writer.reset();
    EXPECT_EQ(provider->engineSession().outstanding().assets, 0u);
    EXPECT_TRUE(provider->owned().shutdown().ok());
}

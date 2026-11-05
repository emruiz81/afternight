// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The engine session: settings checked before anything is built, settings that wait for the next construction, a
/// context that is not replaced while anything taken from it is held, and a failed construction that leaves the
/// session unavailable until it is reopened.

#include "ConsumerRuntimeTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <new>
#include <optional>
#include <stop_token>

namespace
{

using an::consumer::EngineSession;
using an::consumer::EngineSessionSettings;
using an::consumer::SessionState;
using an::tests::consumer::filledAsset;
using an::tests::consumer::filledImage;
using an::tests::consumer::openSession;
using an::tests::consumer::settingsOfWidth;

/// A registry holding the FITS codec, or the failure that stopped it.
[[nodiscard]] an::Result<std::shared_ptr<const an::CodecRegistry>> fitsRegistry()
{
    an::Result<std::shared_ptr<const an::ImageCodec>> fits = an::createFitsCodec();
    if (!fits.hasValue())
    {
        return fits.status();
    }
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> codecs{*fits};
    return an::CodecRegistry::create(codecs);
}

/// A registry factory that fails on the calls a case names and builds the FITS registry otherwise.
class ScriptedRegistry
{
public:
    explicit ScriptedRegistry(std::uint32_t failingCall) : m_failingCall(failingCall) {}

    [[nodiscard]] an::consumer::CodecRegistryFactory factory()
    {
        return [this]() -> an::Result<std::shared_ptr<const an::CodecRegistry>> {
            const std::uint32_t call = ++m_calls;
            if (call == m_failingCall)
            {
                return an::Status::failure(an::ErrorCode::AllocationFailed, "the scripted factory refused this call");
            }
            return fitsRegistry();
        };
    }

    [[nodiscard]] std::uint32_t calls() const noexcept
    {
        return m_calls.load();
    }

private:
    std::uint32_t m_failingCall = 0u;
    std::atomic<std::uint32_t> m_calls{0u};
};

/// A memory resource of the caller's: the platform's aligned memory, counted, and refused while a case says so.
class CountingResource final : public an::MemoryResource
{
public:
    [[nodiscard]] an::Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) override
    {
        ++m_requests;
        if (m_refusing.load())
        {
            return an::Status::failure(an::ErrorCode::AllocationFailed, "the case refuses every request");
        }
        void* block = ::operator new(static_cast<std::size_t>(bytes), std::align_val_t{alignment}, std::nothrow);
        if (block == nullptr)
        {
            return an::Status::failure(an::ErrorCode::AllocationFailed, "the platform refused the request");
        }
        ++m_outstanding;
        return block;
    }

    void deallocate(void* pointer, std::uint64_t, std::uint64_t alignment) noexcept override
    {
        if (pointer != nullptr)
        {
            --m_outstanding;
            ::operator delete(pointer, std::align_val_t{alignment});
        }
    }

    [[nodiscard]] an::MemoryCounters counters() const noexcept override
    {
        return an::MemoryCounters{};
    }

    void refuse(bool refusing) noexcept
    {
        m_refusing = refusing;
    }

    [[nodiscard]] std::uint64_t requests() const noexcept
    {
        return m_requests.load();
    }

    [[nodiscard]] std::int64_t outstanding() const noexcept
    {
        return m_outstanding.load();
    }

private:
    std::atomic<bool> m_refusing{false};
    std::atomic<std::uint64_t> m_requests{0u};
    std::atomic<std::int64_t> m_outstanding{0};
};

} // namespace

// A session built over a resource the caller states takes every block its context allocates from that resource -
// the seam a fault-injection run refuses an allocation at a chosen point through. A refusal reaches the caller as a
// typed failure, and the session works again once the resource does.
TEST(EngineSessionTest, AResourceTheCallerStatesIsWhereTheSessionsContextTakesItsMemory)
{
    CountingResource resource;
    EngineSessionSettings settings = settingsOfWidth(1u);
    settings.memoryResource = &resource;
    an::Result<EngineSession> session = EngineSession::open(settings);
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    {
        std::optional<an::consumer::ImageAsset> asset = filledAsset(*session, 0.5f, 64u, 32u);
        ASSERT_TRUE(asset.has_value());
        EXPECT_GT(resource.requests(), 0u);
        EXPECT_GT(resource.outstanding(), 0) << "the asset's storage came from the caller's resource";
    }
    EXPECT_EQ(resource.outstanding(), 0) << "and went back to it when the asset went";

    resource.refuse(true);
    {
        an::Result<an::consumer::SessionHold> hold = session->hold();
        ASSERT_TRUE(hold.hasValue()) << hold.status().message();
        const an::Result<an::Image<float>> refused = filledImage(hold->engine(), 64u, 32u, 0.5f);
        ASSERT_FALSE(refused.hasValue());
        const an::ErrorCode code = refused.status().code();
        EXPECT_TRUE(code == an::ErrorCode::AllocationFailed || code == an::ErrorCode::MemoryBudgetExceeded)
            << refused.status().message();
    }
    resource.refuse(false);
    EXPECT_TRUE(filledAsset(*session, 0.25f).has_value()) << "the session works again";
}

TEST(EngineSessionTest, OpensWithTheStatedWidthAndHandsOutHolds)
{
    std::optional<EngineSession> session = openSession(3u);
    ASSERT_TRUE(session.has_value());
    EXPECT_TRUE(session->valid());
    EXPECT_EQ(session->state(), SessionState::Available);
    EXPECT_EQ(session->width(), 3u);
    EXPECT_NE(session->contextId(), 0u);
    EXPECT_EQ(session->generation(), 1u);
    EXPECT_TRUE(session->lastFailure().ok());
    EXPECT_FALSE(session->settingsPendingRestart());

    an::Result<an::consumer::SessionHold> hold = session->hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();
    EXPECT_EQ(hold->engine().id(), session->contextId());
    EXPECT_EQ(hold->engine().maxConcurrency(), 3u);
    EXPECT_EQ(session->outstanding().holds, 1u);
    EXPECT_GE(session->outstanding().engineLeases, 1u) << "a hold is a lease of no threads on the context";
    hold->release();
    EXPECT_EQ(session->outstanding().holds, 0u);
}

TEST(EngineSessionTest, RefusesSettingsTheEngineWouldRefuseBeforeBuildingAnything)
{
    EngineSessionSettings tooWide = settingsOfWidth(an::kMaxConfigurableConcurrency + 1u);
    an::Result<EngineSession> refused = EngineSession::open(tooWide);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    ASSERT_NE(refused.status().findContext(an::ContextKey::Limit), nullptr);
    EXPECT_EQ(refused.status().findContext(an::ContextKey::Limit)->count, an::kMaxConfigurableConcurrency);

    EngineSessionSettings noBudget = settingsOfWidth(1u);
    noBudget.limits.memoryBudgetBytes = 0u;
    EXPECT_EQ(EngineSession::open(noBudget).code(), an::ErrorCode::InvalidArgument);

    EngineSessionSettings noQueue = settingsOfWidth(1u);
    noQueue.admissionQueueCapacity = 0u;
    EXPECT_EQ(EngineSession::open(noQueue).code(), an::ErrorCode::InvalidArgument);

    EngineSessionSettings noSnapshots = settingsOfWidth(1u);
    noSnapshots.memory.pendingSnapshots.bytes = 0u;
    const an::Result<EngineSession> zeroAllowance = EngineSession::open(noSnapshots);
    EXPECT_EQ(zeroAllowance.code(), an::ErrorCode::InvalidArgument);
    ASSERT_NE(zeroAllowance.status().findContext(an::ContextKey::Parameter), nullptr);
    EXPECT_EQ(zeroAllowance.status().findContext(an::ContextKey::Parameter)->text, "memory.pendingSnapshots.bytes");

    // The same check refuses a request for later, and records nothing.
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->requestSettings(tooWide).code(), an::ErrorCode::LimitExceeded);
    EXPECT_FALSE(session->settingsPendingRestart());
}

// A change of ceiling is recorded and shown as pending; the running context keeps what it was built with until the
// next construction, which waits for everything taken from it to be released.
TEST(EngineSessionTest, RequestedSettingsWaitForTheNextConstruction)
{
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    const std::uint64_t firstContext = session->contextId();

    EngineSessionSettings wider = settingsOfWidth(4u);
    wider.memory.displayBuffers.bytes = 64u * an::consumer::kMebibyte;
    ASSERT_TRUE(session->requestSettings(wider).ok());

    EXPECT_TRUE(session->settingsPendingRestart());
    ASSERT_TRUE(session->pendingSettings().has_value());
    EXPECT_EQ(session->pendingSettings()->maxConcurrency, 4u);
    EXPECT_EQ(session->activeSettings().maxConcurrency, 2u);
    EXPECT_EQ(session->width(), 2u);
    EXPECT_EQ(session->contextId(), firstContext);
    EXPECT_EQ(session->memory().allowances().displayBuffers.bytes, 512u * an::consumer::kMebibyte)
        << "the allowances change with the rest of the settings, not before";

    {
        std::optional<an::consumer::ImageAsset> asset = filledAsset(*session, 1.0f);
        ASSERT_TRUE(asset.has_value());
        const an::Status refused = session->applyPendingSettings();
        EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
        EXPECT_EQ(session->width(), 2u);
        EXPECT_EQ(session->contextId(), firstContext);
        EXPECT_TRUE(session->settingsPendingRestart()) << "a refused replacement keeps the settings pending";
    }

    ASSERT_TRUE(session->applyPendingSettings().ok());
    EXPECT_EQ(session->state(), SessionState::Available);
    EXPECT_EQ(session->width(), 4u);
    EXPECT_NE(session->contextId(), firstContext);
    EXPECT_EQ(session->generation(), 2u);
    EXPECT_FALSE(session->settingsPendingRestart());
    EXPECT_EQ(session->activeSettings().maxConcurrency, 4u);
    EXPECT_EQ(session->memory().allowances().displayBuffers.bytes, 64u * an::consumer::kMebibyte);

    // Nothing pending is nothing to do.
    EXPECT_TRUE(session->applyPendingSettings().ok());
    EXPECT_EQ(session->generation(), 2u);
}

// An asset holds storage from the context whether or not anything runs; the session refuses to replace a context
// under it and names what is outstanding.
TEST(EngineSessionTest, AnAssetHeldBlocksReplacementAndNothingChanges)
{
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    std::optional<an::consumer::ImageAsset> asset = filledAsset(*session, 3.0f);
    ASSERT_TRUE(asset.has_value());
    ASSERT_TRUE(session->requestSettings(settingsOfWidth(3u)).ok());
    const std::uint64_t context = session->contextId();

    const an::Status refused = session->applyPendingSettings();
    ASSERT_EQ(refused.code(), an::ErrorCode::Busy);
    EXPECT_NE(std::string(refused.message()).find("1 image asset"), std::string::npos) << refused.message();
    EXPECT_EQ(session->outstanding().assets, 1u);
    EXPECT_EQ(session->state(), SessionState::Available);
    EXPECT_EQ(session->contextId(), context);

    // The asset still reads what it held: nothing was torn down under it.
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    EXPECT_EQ(an::tests::consumer::sampleAt(lease->image(), 0u, 0u), 3.0f);
    lease->release();

    asset.reset();
    EXPECT_EQ(session->outstanding().assets, 0u);
    EXPECT_TRUE(session->applyPendingSettings().ok());
    EXPECT_EQ(session->width(), 3u);
}

// Storage created through a hold and kept after it is outstanding in the engine context, though the session counts
// nothing: the engine's own refusal to shut down is what stops the replacement, and the session stays as it was.
TEST(EngineSessionTest, StorageKeptOutsideAnAssetBlocksReplacementThroughTheEngine)
{
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::Image<float>> kept = [&session]() -> an::Result<an::Image<float>> {
        an::Result<an::consumer::SessionHold> hold = session->hold();
        if (!hold.hasValue())
        {
            return hold.status();
        }
        return filledImage(hold->engine(), 4u, 4u, 1.0f);
    }();
    ASSERT_TRUE(kept.hasValue()) << kept.status().message();
    ASSERT_TRUE(session->outstanding().none());
    ASSERT_TRUE(session->requestSettings(settingsOfWidth(3u)).ok());
    const std::uint64_t context = session->contextId();

    const an::Status refused = session->applyPendingSettings();
    ASSERT_EQ(refused.code(), an::ErrorCode::Busy);
    ASSERT_TRUE(refused.hasCause());
    EXPECT_EQ(refused.cause().code(), an::ErrorCode::Busy) << "the engine's own refusal is the cause";
    EXPECT_EQ(session->state(), SessionState::Available);
    EXPECT_EQ(session->contextId(), context);
    EXPECT_TRUE(session->hold().hasValue()) << "the context the session kept still works";

    kept->release();
    EXPECT_TRUE(session->applyPendingSettings().ok());
    EXPECT_NE(session->contextId(), context);
}

// A read lease keeps the asset's state, and with it the asset's storage, after the asset object is gone.
TEST(EngineSessionTest, AReadLeaseOutlivingItsAssetStillBlocksReplacement)
{
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    std::optional<an::consumer::ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    asset.reset();
    ASSERT_TRUE(session->requestSettings(settingsOfWidth(1u)).ok());

    EXPECT_EQ(session->outstanding().assets, 1u);
    EXPECT_EQ(session->applyPendingSettings().code(), an::ErrorCode::Busy);
    lease->release();
    EXPECT_EQ(session->outstanding().assets, 0u);
    EXPECT_TRUE(session->applyPendingSettings().ok());
}

// The previous context is shut down before the replacement is built. When the build then fails the session holds no
// context, says so with the failure, refuses every kind of work, and is reopened from the settings it was asked for.
TEST(EngineSessionTest, AFailedReplacementLeavesTheSessionUnavailableUntilReopened)
{
    ScriptedRegistry registry(2u);
    EngineSessionSettings settings = settingsOfWidth(2u);
    settings.codecRegistry = registry.factory();
    an::Result<EngineSession> opened = EngineSession::open(settings);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    EngineSession& session = *opened;
    const std::uint64_t firstContext = session.contextId();

    EngineSessionSettings wider = settings;
    wider.maxConcurrency = 3u;
    ASSERT_TRUE(session.requestSettings(wider).ok());
    const an::Status failed = session.applyPendingSettings();
    ASSERT_TRUE(failed.failed());
    EXPECT_EQ(registry.calls(), 2u);

    EXPECT_EQ(session.state(), SessionState::Unavailable);
    EXPECT_EQ(session.contextId(), 0u) << "an unavailable session exposes no context";
    EXPECT_EQ(session.width(), 0u);
    EXPECT_TRUE(session.lastFailure().failed());
    EXPECT_TRUE(session.settingsPendingRestart()) << "the settings that failed are still the ones asked for";

    const an::Result<an::consumer::SessionHold> hold = session.hold();
    ASSERT_EQ(hold.code(), an::ErrorCode::NotInitialized);
    ASSERT_TRUE(hold.status().hasCause());
    EXPECT_EQ(hold.status().rootCause().code(), an::ErrorCode::AllocationFailed);
    EXPECT_EQ(session.tryAdmit(an::consumer::OperationRequest{}).code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(session.reservePreview().code(), an::ErrorCode::NotInitialized);

    ASSERT_TRUE(session.reopen().ok());
    EXPECT_EQ(registry.calls(), 3u);
    EXPECT_EQ(session.state(), SessionState::Available);
    EXPECT_EQ(session.width(), 3u);
    EXPECT_NE(session.contextId(), 0u);
    EXPECT_NE(session.contextId(), firstContext);
    EXPECT_FALSE(session.settingsPendingRestart());
    EXPECT_TRUE(session.lastFailure().ok());
    an::Result<an::consumer::SessionHold> again = session.hold();
    ASSERT_TRUE(again.hasValue());
    ASSERT_NE(again->engine().codecRegistry(), nullptr);
    EXPECT_EQ(again->engine().codecRegistry()->size(), 1u);
}

TEST(EngineSessionTest, ReopenIsOnlyForAnUnavailableSession)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->reopen().code(), an::ErrorCode::InvalidState);
}

TEST(EngineSessionTest, TheCodecRegistryFactoryRunsAtEveryConstruction)
{
    ScriptedRegistry registry(0u);
    EngineSessionSettings settings = settingsOfWidth(1u);
    settings.codecRegistry = registry.factory();
    an::Result<EngineSession> session = EngineSession::open(settings);
    ASSERT_TRUE(session.hasValue());
    EXPECT_EQ(registry.calls(), 1u);
    ASSERT_TRUE(session->requestSettings(settings).ok());
    ASSERT_TRUE(session->applyPendingSettings().ok());
    EXPECT_EQ(registry.calls(), 2u);

    // A session given no factory builds a context with no codecs.
    std::optional<EngineSession> bare = openSession(1u);
    ASSERT_TRUE(bare.has_value());
    an::Result<an::consumer::SessionHold> hold = bare->hold();
    ASSERT_TRUE(hold.hasValue());
    EXPECT_EQ(hold->engine().codecRegistry(), nullptr);
}

TEST(EngineSessionTest, ShutdownClosesAdmissionAndRefusesWhileAnAssetIsHeld)
{
    std::optional<EngineSession> session = openSession(2u);
    ASSERT_TRUE(session.has_value());
    std::optional<an::consumer::ImageAsset> asset = filledAsset(*session, 1.0f);
    ASSERT_TRUE(asset.has_value());

    const an::Status refused = session->shutdown();
    EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
    EXPECT_EQ(session->state(), SessionState::Closed) << "admission stays closed after a refused shutdown";
    EXPECT_EQ(session->hold().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(session->tryAdmit(an::consumer::OperationRequest{}).code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(session->requestSettings(settingsOfWidth(1u)).code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(asset->snapshot().code(), an::ErrorCode::InvalidState) << "a closed session admits no new asset";

    asset.reset();
    EXPECT_TRUE(session->shutdown().ok());
    EXPECT_EQ(session->contextId(), 0u);
    EXPECT_TRUE(session->shutdown().ok()) << "shutting down twice shuts down once";
}

TEST(EngineSessionTest, AWaitingRequestIsRefusedWhenAdmissionCloses)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::OperationScope> running = session->admit(an::consumer::OperationRequest{});
    ASSERT_TRUE(running.hasValue());

    std::stop_source cleanup;
    std::future<an::ErrorCode> waiting = std::async(std::launch::async, [&session, token = cleanup.get_token()] {
        return session->admit(an::consumer::OperationRequest{.stopToken = token}).code();
    });
    ASSERT_TRUE(an::tests::consumer::eventually([&session] { return session->admission().queued == 1u; }));

    session->closeAdmission();
    const bool answered = an::tests::consumer::answers(waiting);
    if (!answered)
    {
        cleanup.request_stop();
    }
    ASSERT_TRUE(answered) << "closing admission did not release the waiting request";
    EXPECT_EQ(waiting.get(), an::ErrorCode::InvalidState);
    EXPECT_EQ(session->admission().queued, 0u);
}

TEST(EngineSessionTest, AMovedFromSessionHoldsNothing)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    EngineSession taken = std::move(*session);
    EXPECT_FALSE(session->valid());
    EXPECT_EQ(session->hold().code(), an::ErrorCode::NotInitialized);
    EXPECT_TRUE(taken.valid());
    EXPECT_TRUE(taken.hold().hasValue());
}

TEST(EngineSessionTest, TheLoadedLibraryReportsTheInterfaceVersionItsHeadersState)
{
    EXPECT_EQ(an::consumer::consumerRuntimeInterfaceVersion(), an::consumer::kConsumerRuntimeInterfaceVersion);
}

TEST(EngineSessionTest, StateNamesAreStable)
{
    EXPECT_STREQ(an::consumer::sessionStateName(SessionState::Available), "available");
    EXPECT_STREQ(an::consumer::sessionStateName(SessionState::Replacing), "replacing");
    EXPECT_STREQ(an::consumer::sessionStateName(SessionState::Unavailable), "unavailable");
    EXPECT_STREQ(an::consumer::sessionStateName(SessionState::Closed), "closed");
}

// A replacement waits for everything taken from the context, and what holds it is named rather than guessed at: a
// hold, an admitted operation and a request waiting for admission each refuse it, change nothing, keep the settings
// pending, and once the last of them ends the replacement goes through. Each state is observed through what the
// session counts, not through a pause.
TEST(EngineSessionTest, AReplacementIsRefusedWhileAHoldAnOperationOrAQueuedRequestIsOutstanding)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    const std::uint64_t firstContext = session->contextId();
    ASSERT_TRUE(session->requestSettings(settingsOfWidth(2u)).ok());

    const auto refusedNaming = [&](const char* what) {
        const an::Status refused = session->applyPendingSettings();
        EXPECT_EQ(refused.code(), an::ErrorCode::Busy) << what;
        EXPECT_EQ(session->contextId(), firstContext) << what;
        EXPECT_TRUE(session->settingsPendingRestart()) << what;
    };

    {
        an::Result<an::consumer::SessionHold> hold = session->hold();
        ASSERT_TRUE(hold.hasValue());
        refusedNaming("a hold");
    }
    {
        an::Result<an::consumer::OperationScope> running = session->admit(an::consumer::OperationRequest{});
        ASSERT_TRUE(running.hasValue()) << running.status().message();
        refusedNaming("an operation");

        // The one slot is taken, so a second request waits for admission; the session counts it as queued.
        std::stop_source cleanup;
        std::future<an::Result<an::consumer::OperationScope>> queued =
            std::async(std::launch::async, [&session, token = cleanup.get_token()] {
                return session->admit(an::consumer::OperationRequest{.stopToken = token});
            });
        ASSERT_TRUE(an::tests::consumer::eventually([&] { return session->outstanding().queued == 1u; }));
        running->end();
        // The queued request is admitted the moment the slot is free, and holds the context in its turn.
        ASSERT_TRUE(an::tests::consumer::answers(queued));
        an::Result<an::consumer::OperationScope> admitted = queued.get();
        ASSERT_TRUE(admitted.hasValue()) << admitted.status().message();
        refusedNaming("the operation that was queued");
    }

    ASSERT_TRUE(session->outstanding().none());
    ASSERT_TRUE(session->applyPendingSettings().ok());
    EXPECT_NE(session->contextId(), firstContext);
    EXPECT_EQ(session->width(), 2u);
}

// A shutdown refused while an operation runs closes admission and names what is outstanding; the operation finishes
// in a context that is still there, nothing new is admitted meanwhile, and the shutdown goes through once it ends.
TEST(EngineSessionTest, AShutdownWithAnOperationOutstandingIsRefusedNamingItAndGoesThroughOnceItEnds)
{
    std::optional<EngineSession> session = openSession(1u);
    ASSERT_TRUE(session.has_value());
    an::Result<an::consumer::OperationScope> running = session->admit(an::consumer::OperationRequest{});
    ASSERT_TRUE(running.hasValue()) << running.status().message();

    const an::Status refused = session->shutdown();
    EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
    EXPECT_EQ(session->state(), SessionState::Closed);
    EXPECT_EQ(session->outstanding().operations, 1u);
    EXPECT_FALSE(session->admit(an::consumer::OperationRequest{}).hasValue()) << "nothing is admitted once closed";
    EXPECT_TRUE(running->execution().checkCancellation().ok()) << "the running operation is not stopped by it";

    running->end();
    EXPECT_TRUE(session->shutdown().ok());
    EXPECT_FALSE(session->hold().hasValue());
}

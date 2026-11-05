// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/ExecutionContext.h.
//
// An execution context is one operation's policy and its permission to run. Two things about it are
// worth more than its accessors.
//
// The first is lifetime. It holds a lease on the engine context it was made from, so an engine
// context with work in flight refuses to shut down without anybody having to remember to say so -
// which is the whole reason the lease exists and the first place it carries weight.
//
// The second is that it can narrow and cannot widen. A preview that wants two threads while a batch
// runs is legitimate; an operation that asks for more than the user's setting is not, and the
// refusal is what makes the setting worth having.
//
// The cancellation and sink halves are next door, in ExecutionCancellationTest and
// ExecutionSinksTest, because those are about behaviour over time rather than about construction.

#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>
#include <AfterNightEngine/Runtime/ScratchArena.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

/// A progress sink that does nothing, for the cases that only care the pointer is carried.
class SilentProgressSink final : public an::ProgressSink
{
public:
    void onProgress(const an::ProgressReport&) noexcept override {}
};

/// A diagnostic sink that does nothing, for the same reason.
class SilentDiagnosticSink final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent&) noexcept override {}
};

/// A trace sink that does nothing, for the same reason.
class SilentTraceSink final : public an::TraceSink
{
public:
    void onTrace(const an::TraceEvent&) noexcept override {}
};

/// An engine context every case in this file starts from, with room to narrow below it.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint32_t maxConcurrency = 8u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = maxConcurrency;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

TEST(ExecutionContextTest, IsMoveOnlyBecauseItHoldsALease)
{
    // Copying one would be a second hold on the engine context that no call site asked for, and the
    // count is what decides whether that context may be shut down.
    static_assert(!std::is_copy_constructible_v<an::ExecutionContext>);
    static_assert(!std::is_copy_assignable_v<an::ExecutionContext>);
    static_assert(std::is_nothrow_move_constructible_v<an::ExecutionContext>);
    static_assert(std::is_nothrow_move_assignable_v<an::ExecutionContext>);
    static_assert(std::is_nothrow_default_constructible_v<an::ExecutionContext>);
}

TEST(ExecutionContextTest, ADefaultConstructedExecutionHoldsNothingAndSaysSo)
{
    const an::ExecutionContext execution;

    EXPECT_FALSE(execution.valid());
    EXPECT_EQ(execution.operationId(), 0u);
    EXPECT_EQ(execution.engineContextId(), 0u);
    EXPECT_EQ(execution.maxConcurrency(), 0u);
    EXPECT_EQ(execution.progressSink(), nullptr);
    EXPECT_EQ(execution.diagnosticSink(), nullptr);
    EXPECT_EQ(execution.traceSink(), nullptr);
    EXPECT_EQ(execution.operationCounters().operationId, 0u);
    EXPECT_EQ(execution.scratch(), nullptr);
    EXPECT_FALSE(execution.stopRequested());
    EXPECT_FALSE(execution.deadlineExceeded());
    EXPECT_TRUE(execution.checkCancellation().ok());
}

TEST(ExecutionContextTest, CreatingOneHoldsItsEngineContextOpen)
{
    an::EngineContext engine = makeEngineContext();
    ASSERT_TRUE(engine.quiescent());

    {
        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
        ASSERT_TRUE(execution.hasValue()) << execution.status().message();

        EXPECT_TRUE(execution->valid());
        EXPECT_EQ(execution->engineContextId(), engine.id());
        EXPECT_EQ(engine.outstandingLeases(), 1u);
        EXPECT_FALSE(engine.quiescent());

        // The lease doing its work without anybody remembering to hold one: an engine context with
        // an operation in flight refuses to be shut down.
        const an::Status refused = engine.shutdown();
        EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
        EXPECT_TRUE(engine.valid());
    }

    EXPECT_TRUE(engine.quiescent());
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(ExecutionContextTest, CreatingOneFromAnEngineContextThatHoldsNothingFails)
{
    const an::EngineContext engine;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    ASSERT_FALSE(execution.hasValue());
    EXPECT_EQ(execution.status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(execution.status().domain(), an::ErrorDomain::Runtime);
    EXPECT_STREQ(execution.status().operation(), "ExecutionContext::create");

    // Reported in this entry point's vocabulary, keeping what the engine context said as the cause.
    ASSERT_TRUE(execution.status().hasCause());
    EXPECT_STREQ(execution.status().cause().operation(), "EngineContext::acquireLease");
}

TEST(ExecutionContextTest, WithoutARequestItMayUseEverythingTheEngineContextPermits)
{
    an::EngineContext engine = makeEngineContext(6u);

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    EXPECT_EQ(execution->maxConcurrency(), 6u);
    EXPECT_NE(execution->maxConcurrency(), an::kUseContextConcurrency);
}

TEST(ExecutionContextTest, ItMayNarrowBelowItsEngineContext)
{
    an::EngineContext engine = makeEngineContext(8u);

    an::ExecutionOptions options;
    options.maxConcurrency = 2u;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    // A preview should not take the machine while a batch runs. Narrowing is the whole point of the
    // request existing.
    EXPECT_EQ(execution->maxConcurrency(), 2u);
    EXPECT_EQ(engine.maxConcurrency(), 8u);
}

TEST(ExecutionContextTest, ItMayNotWidenBeyondItsEngineContext)
{
    an::EngineContext engine = makeEngineContext(4u);

    an::ExecutionOptions options;
    options.maxConcurrency = 5u;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_FALSE(execution.hasValue());

    const an::Status& refusal = execution.status();
    EXPECT_EQ(refusal.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(refusal.category(), an::ErrorCategory::Resource);
    EXPECT_EQ(refusal.domain(), an::ErrorDomain::Runtime);
    EXPECT_STREQ(refusal.operation(), "ExecutionContext::create");

    const an::StatusContextField* requested = refusal.findContext(an::ContextKey::Requested);
    const an::StatusContextField* limit = refusal.findContext(an::ContextKey::Limit);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(requested->count, 5u);
    EXPECT_EQ(limit->count, 4u);

    // A refused execution takes nothing with it: no lease was left behind.
    EXPECT_TRUE(engine.quiescent());
}

TEST(ExecutionContextTest, WideningIsValidatedBeforeAConcurrencyLeaseIsTaken)
{
    an::EngineContext engine = makeEngineContext(1u);
    const an::Result<an::EngineContextLease> held = engine.acquireLease(1u);
    ASSERT_TRUE(held.hasValue());

    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);

    ASSERT_FALSE(execution.hasValue());
    EXPECT_EQ(execution.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}

TEST(ExecutionContextTest, ItCarriesTheLimitsOfTheEngineContextItBelongsTo)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 2u;
    engineOptions.limits.maxImageWidth = 4096u;
    engineOptions.limits.memoryBudgetBytes = 16ull * 1024ull * 1024ull;

    an::Result<an::EngineContext> created = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(created.hasValue()) << created.status().message();
    an::EngineContext engine = std::move(*created);

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    // An algorithm validating its own geometry needs the configured maxima, and should reach them
    // through the context it was handed rather than through a global or a second parameter.
    const an::EngineLimits limits = execution->limits();
    EXPECT_EQ(limits.maxImageWidth, 4096u);
    EXPECT_EQ(limits.memoryBudgetBytes, 16ull * 1024ull * 1024ull);
}

TEST(ExecutionContextTest, EveryExecutionHasItsOwnIdentityAndNamesItsEngineContext)
{
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    const an::Result<an::ExecutionContext> first = an::ExecutionContext::create(engine, options);
    const an::Result<an::ExecutionContext> second = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    EXPECT_NE(first->operationId(), 0u);
    EXPECT_NE(second->operationId(), 0u);
    EXPECT_NE(first->operationId(), second->operationId());
    EXPECT_EQ(first->engineContextId(), engine.id());
    EXPECT_EQ(second->engineContextId(), engine.id());
    EXPECT_EQ(engine.outstandingLeases(), 2u);
}

TEST(ExecutionContextTest, SeveralExecutionsShareOneEngineContext)
{
    an::EngineContext engine = makeEngineContext(8u);

    std::vector<an::ExecutionContext> executions;
    for (std::uint32_t index = 1u; index <= 4u; ++index)
    {
        an::ExecutionOptions options;
        options.maxConcurrency = index;

        an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue()) << execution.status().message();
        EXPECT_EQ(execution->maxConcurrency(), index);
        executions.push_back(std::move(*execution));
    }

    EXPECT_EQ(engine.outstandingLeases(), 4u);
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);

    executions.clear();
    EXPECT_TRUE(engine.quiescent());
}

TEST(ExecutionContextTest, MovingAnExecutionMovesTheHoldRatherThanAddingOne)
{
    an::EngineContext engine = makeEngineContext(8u);

    SilentProgressSink progress;
    SilentDiagnosticSink diagnostics;
    SilentTraceSink traces;

    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.progressSink = &progress;
    options.diagnosticSink = &diagnostics;
    options.traceSink = &traces;
    options.backend = an::ComputeBackend::Reference;
    options.backendFallback = an::BackendFallback::Forbidden;
    options.reductionDeterminism = an::ReductionDeterminism::All;
    options.deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);

    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::ExecutionContext source = std::move(*created);
    const std::uint64_t operationId = source.operationId();
    ASSERT_EQ(engine.outstandingLeases(), 1u);

    const an::ExecutionContext target = std::move(source);
    EXPECT_TRUE(target.valid());
    EXPECT_EQ(target.operationId(), operationId);
    EXPECT_EQ(target.maxConcurrency(), 3u);
    EXPECT_EQ(target.backend(), an::ComputeBackend::Reference);
    EXPECT_EQ(target.progressSink(), &progress);
    EXPECT_EQ(target.diagnosticSink(), &diagnostics);
    EXPECT_EQ(target.traceSink(), &traces);
    EXPECT_EQ(target.operationCounters().operationId, operationId);
    EXPECT_EQ(engine.outstandingLeases(), 1u);

    // "Moved from" and "holds nothing" are the same state rather than two that mostly agree: a
    // defaulted move would have carried the lease away and left every scalar readable behind it,
    // so an execution context that owns no operation would still report one's identity, its share
    // of the machine and its policy.
    EXPECT_FALSE(source.valid());
    EXPECT_EQ(source.operationId(), 0u);
    EXPECT_EQ(source.engineContextId(), 0u);
    EXPECT_EQ(source.maxConcurrency(), 0u);
    EXPECT_EQ(source.limits().maxImageWidth, 0u);
    EXPECT_EQ(source.progressSink(), nullptr);
    EXPECT_EQ(source.diagnosticSink(), nullptr);
    EXPECT_EQ(source.traceSink(), nullptr);
    EXPECT_EQ(source.operationCounters().operationId, 0u);
    EXPECT_EQ(source.scratch(), nullptr);
    EXPECT_FALSE(source.deadline().has_value());
    EXPECT_FALSE(source.stopToken().stop_possible());
    EXPECT_EQ(source.requestedBackend(), an::ComputeBackend::Automatic);
    EXPECT_EQ(source.backend(), an::ComputeBackend::Automatic);
    EXPECT_EQ(source.backendFallback(), an::BackendFallback::Allowed);
    EXPECT_EQ(source.reductionDeterminism(), an::ReductionDeterminism::DecisionFeeding);
}

TEST(ExecutionContextTest, MoveAssignmentReleasesTheHoldItReplaces)
{
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> first = an::ExecutionContext::create(engine, options);
    an::Result<an::ExecutionContext> second = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    ASSERT_EQ(engine.outstandingLeases(), 2u);

    an::ExecutionContext held = std::move(*first);
    const std::uint64_t replacedId = held.operationId();

    held = std::move(*second);
    EXPECT_TRUE(held.valid());
    EXPECT_NE(held.operationId(), replacedId);

    // The execution being overwritten gives its lease back here rather than leaking it, so the
    // engine context's count goes down by one and not by none.
    EXPECT_EQ(engine.outstandingLeases(), 1u);
}

TEST(ExecutionContextTest, TheDefaultPolicyIsTheApprovedBaseline)
{
    an::EngineContext engine = makeEngineContext();

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    // No mode matrix: there is one numeric contract, and the only dimension a caller sets is whether
    // the reductions that cannot change a decision must also be thread-count independent.
    EXPECT_EQ(execution->reductionDeterminism(), an::ReductionDeterminism::DecisionFeeding);
    EXPECT_EQ(execution->requestedBackend(), an::ComputeBackend::Automatic);
    EXPECT_EQ(execution->backend(), an::ComputeBackend::Automatic);
    EXPECT_EQ(execution->backendFallback(), an::BackendFallback::Allowed);
}

TEST(ExecutionContextTest, ItKeepsTheNumericRuleItWasGiven)
{
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.reductionDeterminism = an::ReductionDeterminism::All;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    EXPECT_EQ(execution->reductionDeterminism(), an::ReductionDeterminism::All);
}

TEST(ExecutionContextTest, ItKeepsABackendThisBuildAlwaysHas)
{
    an::EngineContext engine = makeEngineContext();

    for (const an::ComputeBackend backend : {an::ComputeBackend::Reference, an::ComputeBackend::Cpu})
    {
        for (const an::BackendFallback fallback : {an::BackendFallback::Allowed, an::BackendFallback::Forbidden})
        {
            an::ExecutionOptions options;
            options.backend = backend;
            options.backendFallback = fallback;

            const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
            ASSERT_TRUE(execution.hasValue())
                << an::computeBackendName(backend) << ": " << execution.status().message();
            EXPECT_EQ(execution->requestedBackend(), backend);
            EXPECT_EQ(execution->backend(), backend);
        }
    }
}

TEST(ExecutionContextTest, ItRefusesABackendThisBuildDoesNotHaveWhenNoFallbackIsPermitted)
{
    ASSERT_FALSE(an::engineCapabilities().gpuBackend) << "this case describes a build without the GPU backend";

    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Gpu;
    options.backendFallback = an::BackendFallback::Forbidden;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_FALSE(execution.hasValue());
    EXPECT_EQ(execution.status().code(), an::ErrorCode::UnsupportedBackend);
    EXPECT_EQ(execution.status().category(), an::ErrorCategory::Capability);
    EXPECT_STREQ(execution.status().operation(), "ExecutionContext::create");

    // Refused at construction rather than at the first kernel, because nothing this execution could
    // do would succeed - a conformance run that forces a backend wants to hear that immediately.
    EXPECT_TRUE(engine.quiescent());
}

TEST(ExecutionContextTest, ItDropsABackendThisBuildDoesNotHaveWhenFallbackIsPermitted)
{
    ASSERT_FALSE(an::engineCapabilities().gpuBackend) << "this case describes a build without the GPU backend";

    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Gpu;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    // What the caller asked for is kept, and what will run is unconstrained rather than a claim
    // about which backend the dispatch table will pick. The difference between the two is how a
    // caller learns its preference was not met.
    EXPECT_EQ(execution->requestedBackend(), an::ComputeBackend::Gpu);
    EXPECT_EQ(execution->backend(), an::ComputeBackend::Automatic);
}

TEST(ExecutionContextTest, ItCarriesTheScratchArenaItWasGivenWithoutOwningIt)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ScratchArena> created = an::ScratchArena::create(engine);
    ASSERT_TRUE(created.hasValue());
    an::ScratchArena* const scratch = &*created;

    an::ExecutionOptions options;
    options.scratch = scratch;
    options.maxConcurrency = 1u;

    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    EXPECT_EQ(execution->scratch(), scratch);

    an::ExecutionOptions withoutOptions;
    withoutOptions.maxConcurrency = 1u;
    const an::Result<an::ExecutionContext> without = an::ExecutionContext::create(engine, withoutOptions);
    ASSERT_TRUE(without.hasValue()) << without.status().message();
    EXPECT_EQ(without->scratch(), nullptr);
}

} // namespace

TEST(ExecutionContextTest, AllReductionsUseAFixedAdmissionRegardlessOfTheRequestedWidth)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 8u;
    auto engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue());
    for (const auto width : {1u, 2u, 8u})
    {
        an::ExecutionOptions options;
        options.maxConcurrency = width;
        options.reductionDeterminism = an::ReductionDeterminism::All;
        auto execution = an::ExecutionContext::create(*engine, options);
        ASSERT_TRUE(execution.hasValue());
        EXPECT_EQ(execution->maxConcurrency(), width);
        EXPECT_EQ(execution->admittedConcurrency(), 1u);
    }
}

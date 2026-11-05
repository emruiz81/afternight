// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/EngineContext.h.
//
// Four properties carry this type, and each of them is something easy to get
// wrong. The context is an owned object rather than a static facade, so two of them coexist and
// neither is the process's. Its maximum concurrency is fixed when it is built, so no call site can
// widen the number a user set. Construction is validated and reports why it refused. And a context
// that still has work outstanding refuses to shut down instead of pulling the ground out from
// under it.
//
// The maximum being immutable is tested by what is absent as much as by what is asserted: there is
// no setter to call, so the cases exercise the operations that would plausibly change it - leases
// taken and released, a replacement refused - and require the number to be exactly what
// construction resolved.

#include <AfterNightEngine/Runtime/EngineContext.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace
{

/// Options a case can vary one field of without restating the rest.
[[nodiscard]] an::EngineContextOptions optionsWithConcurrency(std::uint32_t maxConcurrency)
{
    an::EngineContextOptions options;
    options.maxConcurrency = maxConcurrency;
    return options;
}

/// A context built with a stated maximum, for the cases that are not about construction failing.
[[nodiscard]] an::Result<an::EngineContext> contextWithConcurrency(std::uint32_t maxConcurrency)
{
    return an::EngineContext::create(optionsWithConcurrency(maxConcurrency));
}

/// Default limits with one field zeroed, which is the shape of every unusable configuration.
[[nodiscard]] an::EngineContextOptions optionsWithoutLimit(std::uint32_t an::EngineLimits::* field)
{
    an::EngineContextOptions options;
    options.limits.*field = 0u;
    return options;
}

/// The same for the two limits that are counted in bytes.
[[nodiscard]] an::EngineContextOptions optionsWithoutByteLimit(std::uint64_t an::EngineLimits::* field)
{
    an::EngineContextOptions options;
    options.limits.*field = 0u;
    return options;
}

TEST(EngineContextTest, IsMoveOnlyAndCarriesNoStateOfItsOwn)
{
    // Move-only is the lifetime rule expressed in the type system: a copied context would be two
    // owners of one set of services, and the second destructor would release what the first still
    // uses. The copy operations are deleted rather than merely undeclared, so the diagnostic names
    // the rule.
    static_assert(!std::is_copy_constructible_v<an::EngineContext>);
    static_assert(!std::is_copy_assignable_v<an::EngineContext>);
    static_assert(std::is_move_constructible_v<an::EngineContext>);
    static_assert(std::is_move_assignable_v<an::EngineContext>);
    static_assert(std::is_nothrow_move_constructible_v<an::EngineContext>);
    static_assert(std::is_nothrow_move_assignable_v<an::EngineContext>);
    static_assert(std::is_nothrow_default_constructible_v<an::EngineContext>);
}

TEST(EngineContextTest, ADefaultConstructedContextHoldsNothingAndSaysSo)
{
    const an::EngineContext context;

    EXPECT_FALSE(context.valid());
    EXPECT_EQ(context.id(), 0u);
    EXPECT_EQ(context.maxConcurrency(), 0u);
    EXPECT_EQ(context.outstandingLeases(), 0u);
    EXPECT_TRUE(context.quiescent());

    // Every configured maximum is zero, which is a set of limits that admits nothing - the honest
    // answer for a context that is not there, and one no live context can report.
    const an::EngineLimits limits = context.limits();
    EXPECT_EQ(limits.maxImageWidth, 0u);
    EXPECT_EQ(limits.maxImageHeight, 0u);
    EXPECT_EQ(limits.maxChannelCount, 0u);
    EXPECT_EQ(limits.maxImageBytes, 0u);
    EXPECT_EQ(limits.memoryBudgetBytes, 0u);
}

TEST(EngineContextTest, CreatingOneResolvesTheHostsConcurrencyWhenTheCallerStatesNone)
{
    const an::Result<an::EngineContext> context = an::EngineContext::create(an::EngineContextOptions{});
    ASSERT_TRUE(context.hasValue()) << context.status().message();

    // `kUseHostConcurrency` is a request, not a value: what the context reports afterwards is the
    // number it resolved, and it is never the request itself.
    EXPECT_NE(context->maxConcurrency(), an::kUseHostConcurrency);
    EXPECT_LE(context->maxConcurrency(), an::kMaxConfigurableConcurrency);

    const std::uint32_t host = an::engineCapabilities().hostConcurrency;
    const std::uint32_t expected = host > an::kMaxConfigurableConcurrency ? an::kMaxConfigurableConcurrency : host;
    EXPECT_EQ(context->maxConcurrency(), expected);
}

TEST(EngineContextTest, CreatingOneKeepsAnExplicitMaximumExactly)
{
    const an::Result<an::EngineContext> context = contextWithConcurrency(3u);
    ASSERT_TRUE(context.hasValue()) << context.status().message();

    // Three means three, whatever the machine has. The setting is a ceiling the caller chose, and
    // an engine that rounded it up to the host would be ignoring it.
    EXPECT_EQ(context->maxConcurrency(), 3u);
}

TEST(EngineContextTest, CreatingOneAcceptsTheCeilingAndRefusesAnythingAboveIt)
{
    const an::Result<an::EngineContext> atTheCeiling = contextWithConcurrency(an::kMaxConfigurableConcurrency);
    ASSERT_TRUE(atTheCeiling.hasValue()) << atTheCeiling.status().message();
    EXPECT_EQ(atTheCeiling->maxConcurrency(), an::kMaxConfigurableConcurrency);

    const an::Result<an::EngineContext> above = contextWithConcurrency(an::kMaxConfigurableConcurrency + 1u);
    ASSERT_FALSE(above.hasValue());

    const an::Status& refusal = above.status();
    EXPECT_EQ(refusal.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(refusal.category(), an::ErrorCategory::Resource);
    EXPECT_EQ(refusal.domain(), an::ErrorDomain::Runtime);
    EXPECT_STREQ(refusal.operation(), "EngineContext::create");

    // The two numbers a caller acts on, typed rather than spelled into the message.
    const an::StatusContextField* requested = refusal.findContext(an::ContextKey::Requested);
    const an::StatusContextField* limit = refusal.findContext(an::ContextKey::Limit);
    ASSERT_NE(requested, nullptr);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(requested->count, static_cast<std::uint64_t>(an::kMaxConfigurableConcurrency) + 1u);
    EXPECT_EQ(limit->count, static_cast<std::uint64_t>(an::kMaxConfigurableConcurrency));
}

TEST(EngineContextTest, CreatingOneRefusesLimitsThatAdmitNothing)
{
    // A configured maximum of zero is not a small limit, it is a limit that refuses every image
    // there is. Each field is checked on its own, and the refusal names which one it was, because
    // a caller that mis-set one field cannot find it from a code alone.
    struct Case
    {
        const char* parameter;
        an::EngineContextOptions options;
    };

    const Case cases[] = {{"limits.maxImageWidth", optionsWithoutLimit(&an::EngineLimits::maxImageWidth)},
                          {"limits.maxImageHeight", optionsWithoutLimit(&an::EngineLimits::maxImageHeight)},
                          {"limits.maxChannelCount", optionsWithoutLimit(&an::EngineLimits::maxChannelCount)},
                          {"limits.maxImageBytes", optionsWithoutByteLimit(&an::EngineLimits::maxImageBytes)},
                          {"limits.memoryBudgetBytes", optionsWithoutByteLimit(&an::EngineLimits::memoryBudgetBytes)}};

    for (const Case& testCase : cases)
    {
        const an::Result<an::EngineContext> refused = an::EngineContext::create(testCase.options);
        ASSERT_FALSE(refused.hasValue()) << testCase.parameter;
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument) << testCase.parameter;
        EXPECT_EQ(refused.status().category(), an::ErrorCategory::Argument) << testCase.parameter;
        EXPECT_EQ(refused.status().domain(), an::ErrorDomain::Runtime) << testCase.parameter;
        EXPECT_STREQ(refused.status().operation(), "EngineContext::create") << testCase.parameter;

        const an::StatusContextField* parameter = refused.status().findContext(an::ContextKey::Parameter);
        ASSERT_NE(parameter, nullptr) << testCase.parameter;
        EXPECT_EQ(parameter->text, testCase.parameter);
    }
}

TEST(EngineContextTest, CreatingOneRefusesTheConcurrencyBeforeTheLimits)
{
    // Refusal order is fixed and tested so it cannot drift: the headline setting is answered
    // first, so raising a limit never turns one refusal into a different one.
    an::EngineContextOptions options = optionsWithConcurrency(an::kMaxConfigurableConcurrency + 1u);
    options.limits.maxImageWidth = 0u;

    const an::Result<an::EngineContext> refused = an::EngineContext::create(options);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::LimitExceeded);
}

TEST(EngineContextTest, KeepsTheLimitsItWasBuiltWith)
{
    an::EngineContextOptions options;
    options.limits.maxImageWidth = 4096u;
    options.limits.maxImageHeight = 2048u;
    options.limits.maxChannelCount = 3u;
    options.limits.maxImageBytes = 1024u * 1024u;
    options.limits.memoryBudgetBytes = 8ull * 1024ull * 1024ull;

    const an::Result<an::EngineContext> context = an::EngineContext::create(options);
    ASSERT_TRUE(context.hasValue()) << context.status().message();

    const an::EngineLimits held = context->limits();
    EXPECT_EQ(held.maxImageWidth, 4096u);
    EXPECT_EQ(held.maxImageHeight, 2048u);
    EXPECT_EQ(held.maxChannelCount, 3u);
    EXPECT_EQ(held.maxImageBytes, 1024u * 1024u);
    EXPECT_EQ(held.memoryBudgetBytes, 8ull * 1024ull * 1024ull);
}

TEST(EngineContextTest, TwoContextsCoexistAndNeitherIsTheProcessSetting)
{
    const an::Result<an::EngineContext> narrow = contextWithConcurrency(1u);
    const an::Result<an::EngineContext> wide = contextWithConcurrency(8u);
    ASSERT_TRUE(narrow.hasValue()) << narrow.status().message();
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();

    // The property a static engine facade cannot have: the second one does not overwrite the
    // first, and neither of them is "the" engine.
    EXPECT_EQ(narrow->maxConcurrency(), 1u);
    EXPECT_EQ(wide->maxConcurrency(), 8u);
    EXPECT_NE(narrow->id(), wide->id());
    EXPECT_NE(narrow->id(), 0u);
    EXPECT_NE(wide->id(), 0u);
}

TEST(EngineContextTest, TheEngineNeedsNoContextToExistAndOutlivesEveryOne)
{
    // Create and destroy, twice over, with nothing alive in between. A hidden singleton behind the
    // first construction would show up here as a second one behaving differently from the first.
    std::uint64_t firstId = 0u;
    {
        const an::Result<an::EngineContext> first = contextWithConcurrency(2u);
        ASSERT_TRUE(first.hasValue()) << first.status().message();
        firstId = first->id();
        EXPECT_EQ(first->maxConcurrency(), 2u);
    }

    const an::Result<an::EngineContext> second = contextWithConcurrency(2u);
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_EQ(second->maxConcurrency(), 2u);
    EXPECT_NE(second->id(), firstId);

    // And the build's own capabilities are still answerable, as they were before any of this.
    EXPECT_GE(an::engineCapabilities().hostConcurrency, 1u);
}

TEST(EngineContextTest, MovingAContextTransfersItAndEmptiesTheSource)
{
    an::Result<an::EngineContext> created = contextWithConcurrency(4u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::EngineContext source = std::move(*created);
    const std::uint64_t id = source.id();
    ASSERT_TRUE(source.valid());

    const an::EngineContext target = std::move(source);
    EXPECT_TRUE(target.valid());
    EXPECT_EQ(target.id(), id);
    EXPECT_EQ(target.maxConcurrency(), 4u);

    EXPECT_FALSE(source.valid());
    EXPECT_EQ(source.id(), 0u);
    EXPECT_EQ(source.maxConcurrency(), 0u);
}

TEST(EngineContextTest, MoveAssignmentReleasesWhatItReplaces)
{
    an::Result<an::EngineContext> first = contextWithConcurrency(1u);
    an::Result<an::EngineContext> second = contextWithConcurrency(2u);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    an::EngineContext held = std::move(*first);
    const std::uint64_t replacedId = held.id();

    held = std::move(*second);
    EXPECT_TRUE(held.valid());
    EXPECT_EQ(held.maxConcurrency(), 2u);
    EXPECT_NE(held.id(), replacedId);
}

TEST(EngineContextTest, ShuttingDownAQuiescentContextEmptiesItAndSaysSoOnceMore)
{
    an::Result<an::EngineContext> created = contextWithConcurrency(2u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::EngineContext context = std::move(*created);
    ASSERT_TRUE(context.quiescent());

    EXPECT_TRUE(context.shutdown().ok());
    EXPECT_FALSE(context.valid());

    // Idempotent on purpose: cleanup code that shuts a context down and then lets a scope guard do
    // it again is correct, not an error to report.
    EXPECT_TRUE(context.shutdown().ok());
    EXPECT_FALSE(context.valid());
}

TEST(EngineContextTest, ShuttingDownRefusesWhileWorkIsOutstandingAndChangesNothing)
{
    an::Result<an::EngineContext> created = contextWithConcurrency(2u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::EngineContext context = std::move(*created);
    const std::uint64_t id = context.id();

    an::Result<an::EngineContextLease> lease = context.acquireLease();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    ASSERT_FALSE(context.quiescent());

    const an::Status refused = context.shutdown();
    EXPECT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
    EXPECT_EQ(refused.category(), an::ErrorCategory::State);
    EXPECT_EQ(refused.domain(), an::ErrorDomain::Runtime);
    EXPECT_STREQ(refused.operation(), "EngineContext::shutdown");

    // A refused shutdown is not a partial one: the context is exactly as it was.
    EXPECT_TRUE(context.valid());
    EXPECT_EQ(context.id(), id);
    EXPECT_EQ(context.maxConcurrency(), 2u);
    EXPECT_EQ(context.outstandingLeases(), 1u);

    lease->release();
    EXPECT_TRUE(context.quiescent());
    EXPECT_TRUE(context.shutdown().ok());
}

TEST(EngineContextTest, AnEmptyContextHasNothingToShutDownAndNothingToLease)
{
    an::EngineContext context;

    EXPECT_TRUE(context.shutdown().ok());

    const an::Result<an::EngineContextLease> lease = context.acquireLease();
    ASSERT_FALSE(lease.hasValue());
    EXPECT_EQ(lease.status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(lease.status().domain(), an::ErrorDomain::Runtime);
}

TEST(EngineContextTest, ChangingTheMaximumMeansReplacingTheWholeContext)
{
    an::Result<an::EngineContext> created = contextWithConcurrency(2u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::EngineContext context = std::move(*created);
    const std::uint64_t replacedId = context.id();

    const an::Result<an::EngineContext> replacement =
        an::EngineContext::createReplacement(context, optionsWithConcurrency(6u));
    ASSERT_TRUE(replacement.hasValue()) << replacement.status().message();

    // The old one is gone rather than reconfigured, and the new one is a different context with
    // the new setting. Nothing is carried across, because nothing is reinitialised in place.
    EXPECT_FALSE(context.valid());
    EXPECT_EQ(replacement->maxConcurrency(), 6u);
    EXPECT_NE(replacement->id(), replacedId);
    EXPECT_NE(replacement->id(), 0u);
}

TEST(EngineContextTest, ReplacingRefusesWhileThePreviousContextIsBusyAndLeavesItUsable)
{
    an::Result<an::EngineContext> created = contextWithConcurrency(2u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::EngineContext context = std::move(*created);
    const std::uint64_t id = context.id();

    an::Result<an::EngineContextLease> lease = context.acquireLease();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();

    const an::Result<an::EngineContext> refused =
        an::EngineContext::createReplacement(context, optionsWithConcurrency(6u));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Busy);
    EXPECT_STREQ(refused.status().operation(), "EngineContext::createReplacement");

    // Reported in its own vocabulary without discarding what it was told: the refusal that came
    // back from the shutdown half is the cause, so a report can show either level.
    ASSERT_TRUE(refused.status().hasCause());
    EXPECT_EQ(refused.status().cause().code(), an::ErrorCode::Busy);
    EXPECT_STREQ(refused.status().cause().operation(), "EngineContext::shutdown");

    // The caller keeps what it had. A replacement that consumed the previous context before
    // discovering it was busy would leave the caller with neither.
    EXPECT_TRUE(context.valid());
    EXPECT_EQ(context.id(), id);
    EXPECT_EQ(context.maxConcurrency(), 2u);

    // And the setting it was refused for is still the old one, not half of the new one.
    lease->release();
    EXPECT_EQ(context.maxConcurrency(), 2u);
}

TEST(EngineContextTest, ReplacingRefusesAnUnusableConfigurationBeforeTouchingThePreviousContext)
{
    an::Result<an::EngineContext> created = contextWithConcurrency(2u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::EngineContext context = std::move(*created);
    const std::uint64_t id = context.id();

    const an::Result<an::EngineContext> refused =
        an::EngineContext::createReplacement(context, optionsWithConcurrency(an::kMaxConfigurableConcurrency + 1u));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_STREQ(refused.status().operation(), "EngineContext::createReplacement");

    // Options are answered before the previous context is touched, so a configuration mistake
    // costs the caller nothing. The one failure that cannot be ordered away is an allocation that
    // refuses after the old context is already down, which is why the caller is told to treat a
    // failed replacement as having no context rather than as still holding the old one.
    EXPECT_TRUE(context.valid());
    EXPECT_EQ(context.id(), id);
    EXPECT_EQ(context.maxConcurrency(), 2u);
}

TEST(EngineContextTest, TheMaximumIsWhatConstructionResolvedAndStaysThere)
{
    an::Result<an::EngineContext> created = contextWithConcurrency(5u);
    ASSERT_TRUE(created.hasValue()) << created.status().message();

    an::EngineContext context = std::move(*created);
    ASSERT_EQ(context.maxConcurrency(), 5u);

    // There is no setter, so what this case can do is exercise everything that might plausibly
    // move the number and require that none of it does.
    {
        const an::Result<an::EngineContextLease> first = context.acquireLease();
        const an::Result<an::EngineContextLease> second = context.acquireLease();
        ASSERT_TRUE(first.hasValue());
        ASSERT_TRUE(second.hasValue());
        EXPECT_EQ(context.outstandingLeases(), 2u);
        EXPECT_EQ(context.maxConcurrency(), 5u);

        EXPECT_TRUE(an::EngineContext::createReplacement(context, optionsWithConcurrency(9u)).status().failed());
        EXPECT_EQ(context.maxConcurrency(), 5u);
    }

    EXPECT_TRUE(context.quiescent());
    EXPECT_EQ(context.maxConcurrency(), 5u);
}

} // namespace

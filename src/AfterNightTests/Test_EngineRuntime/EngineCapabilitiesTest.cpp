// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/EngineCapabilities.h.
//
// What is worth testing about a capability report is not the values - they are whatever this
// machine and this build happen to be - but the promises made about them: that the report is
// answerable before anything is constructed, that it does not change while the process runs, and
// that the numbers it carries are usable rather than merely present. A host concurrency of zero
// would be the interesting failure, because it is what a platform that cannot tell returns and it
// would silently become a context configured to run nothing.

#include <AfterNightEngine/Runtime/EngineCapabilities.h>

#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

namespace
{

TEST(EngineCapabilitiesTest, ReportsTheLoadedLibraryRatherThanTheCallersHeaders)
{
    const an::EngineCapabilities capabilities = an::engineCapabilities();

    // The values come out of the library, so they are the exported ones rather than the constants
    // this translation unit was compiled with. Everything here is built from one checkout, so the
    // two agree - and that they agree is the check, because a caller reading a stale header would
    // see them disagree.
    EXPECT_EQ(capabilities.version, an::engineVersion());
    EXPECT_EQ(capabilities.abiGeneration, an::engineAbiGeneration());
    EXPECT_EQ(capabilities.version, an::engineHeaderVersion());
    EXPECT_EQ(capabilities.abiGeneration, an::kEngineAbiGeneration);
}

TEST(EngineCapabilitiesTest, IsAnsweredWithNoContextInExistence)
{
    // The point of asking the build what it can do before building anything: a caller decides how
    // to configure a context from this, so needing a context to read it would be a cycle. Nothing
    // in this case constructs one.
    const an::EngineCapabilities first = an::engineCapabilities();
    const an::EngineCapabilities second = an::engineCapabilities();

    EXPECT_EQ(first.version, second.version);
    EXPECT_EQ(first.abiGeneration, second.abiGeneration);
    EXPECT_EQ(first.hostConcurrency, second.hostConcurrency);
    EXPECT_EQ(first.gpuBackend, second.gpuBackend);
}

TEST(EngineCapabilitiesTest, ReportsAHostConcurrencyThatCanActuallyBeUsed)
{
    const an::EngineCapabilities capabilities = an::engineCapabilities();

    // Never zero. `std::thread::hardware_concurrency` is documented to return zero when the value
    // is not computable, and a context that resolved its maximum from that would be configured to
    // admit nothing at all.
    EXPECT_GE(capabilities.hostConcurrency, 1u);

    // And never so large that the engine would refuse its own default. The configurable ceiling is
    // a filter against a nonsense request, so a host above it is clamped rather than refused.
    const an::Result<an::EngineContext> context = an::EngineContext::create(an::EngineContextOptions{});
    ASSERT_TRUE(context.hasValue()) << context.status().message();
    EXPECT_LE(context->maxConcurrency(), an::kMaxConfigurableConcurrency);
    EXPECT_GE(context->maxConcurrency(), 1u);
}

TEST(EngineCapabilitiesTest, DoesNotClaimAModuleThisBuildDoesNotContain)
{
    // The GPU backend is the one module of this engine a build may leave out entirely, and the
    // build this repository ships leaves it out. The assertion is deliberately the specific one:
    // when the module is added, this case is what says the report has to be updated with it.
    EXPECT_FALSE(an::engineCapabilities().gpuBackend);
}

TEST(EngineCapabilitiesTest, IsWhatAContextReports)
{
    const an::Result<an::EngineContext> context = an::EngineContext::create(an::EngineContextOptions{});
    ASSERT_TRUE(context.hasValue()) << context.status().message();

    const an::EngineCapabilities direct = an::engineCapabilities();
    const an::EngineCapabilities throughContext = context->capabilities();

    // One answer, two ways to reach it. A caller holding a context should not have to know that a
    // free function exists, and the two must not be able to drift apart.
    EXPECT_EQ(throughContext.version, direct.version);
    EXPECT_EQ(throughContext.abiGeneration, direct.abiGeneration);
    EXPECT_EQ(throughContext.hostConcurrency, direct.hostConcurrency);
    EXPECT_EQ(throughContext.gpuBackend, direct.gpuBackend);
}

} // namespace

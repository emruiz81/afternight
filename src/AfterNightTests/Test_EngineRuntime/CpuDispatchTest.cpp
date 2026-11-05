// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/CpuCapabilities.h and
// AfterNightEngine/Runtime/BackendDispatch.h.
//
// A conformance run compares an optimised kernel against the scalar reference, and it is worthless
// unless the engine can be made to run each of them on demand and then say which one it actually
// ran. That is the whole of what this pair of headers is for, and the cases below are about the
// three ways it can quietly stop being true.
//
// **Detection that is not immutable.** What a processor supports does not change while a process
// runs, so it is answered once and the answer is a constant afterwards. A detection that
// re-derived itself, or that raced on first use, would give two answers in one run and a
// conformance comparison would be against whichever one won.
//
// **A forced choice that is honoured only when convenient.** An operation that asked for the
// reference backend and got the fast one silently compares a kernel against itself. So a request
// this build or this host cannot serve is either refused outright or narrowed to something it can
// serve, and never quietly ignored.
//
// **A report that is derived rather than observed.** The selection has to come from the dispatch
// table itself rather than from a second calculation that agrees with it by construction, or the
// report says what the engine intended rather than what it did.

#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// An engine context every case in this file starts from.
///
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// A diagnostic sink that keeps what it was told.
class RecordingDiagnosticSink final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        const std::lock_guard<std::mutex> held(m_guard);
        m_messages.emplace_back(event.message);
        m_severities.push_back(event.severity);
        m_operationIds.push_back(event.operationId);
    }

    /// Whether any recorded message contains a fragment.
    ///
    /// @param fragment the text to look for.
    /// @return true when one message contains it.
    [[nodiscard]] bool mentions(const char* fragment) const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        for (const std::string& message : m_messages)
        {
            if (message.find(fragment) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    /// How many events arrived.
    ///
    /// @return the count.
    [[nodiscard]] std::size_t count() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_messages.size();
    }

    /// Which operation the last event named.
    ///
    /// @return the identity, or zero when nothing arrived.
    [[nodiscard]] std::uint64_t lastOperationId() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_operationIds.empty() ? 0u : m_operationIds.back();
    }

    /// How serious the last event was.
    ///
    /// @return the severity, or `Error` when nothing arrived.
    [[nodiscard]] an::DiagnosticSeverity lastSeverity() const
    {
        const std::lock_guard<std::mutex> held(m_guard);
        return m_severities.empty() ? an::DiagnosticSeverity::Error : m_severities.back();
    }

private:
    /// Guards every member, because events may arrive on several threads.
    mutable std::mutex m_guard;

    /// What each event said.
    std::vector<std::string> m_messages;

    /// How serious each event was.
    std::vector<an::DiagnosticSeverity> m_severities;

    /// Which operation each event came from.
    std::vector<std::uint64_t> m_operationIds;
};

} // namespace

// ---------------------------------------------------------------------------------------------
// Feature detection.
// ---------------------------------------------------------------------------------------------

TEST(CpuCapabilitiesTest, TheAnswerIsTheSameEveryTimeItIsAsked)
{
    const an::CpuCapabilities first = an::cpuCapabilities();
    const an::CpuCapabilities second = an::cpuCapabilities();

    EXPECT_EQ(first.highestSupported, second.highestSupported);
    EXPECT_EQ(first.cacheLineBytes, second.cacheLineBytes);
    EXPECT_EQ(std::memcmp(&first, &second, sizeof(an::CpuCapabilities)), 0);
}

TEST(CpuCapabilitiesTest, TheReportIsInternallyConsistent)
{
    const an::CpuCapabilities capabilities = an::cpuCapabilities();

    // The instruction sets are a ladder: a processor that has one has every one below it. A report
    // that broke this would make "highest supported" meaningless as a dispatch decision.
    if (capabilities.avx2)
    {
        EXPECT_TRUE(capabilities.avx);
        EXPECT_TRUE(capabilities.fma);
        EXPECT_TRUE(capabilities.sse42);
        EXPECT_TRUE(capabilities.sse2);
    }
    if (capabilities.avx512f)
    {
        EXPECT_TRUE(capabilities.avx2);
    }

    EXPECT_TRUE(an::supportsInstructionSet(capabilities, an::InstructionSet::Scalar));
    EXPECT_TRUE(an::supportsInstructionSet(capabilities, capabilities.highestSupported));
    EXPECT_GT(capabilities.cacheLineBytes, 0u);
}

TEST(CpuCapabilitiesTest, Avx2RequiresFmaForThePublishedNumericContract)
{
    an::CpuCapabilities capabilities{};
    capabilities.sse2 = true;
    capabilities.sse42 = true;
    capabilities.avx = true;
    capabilities.avx2 = true;
    capabilities.fma = false;

    EXPECT_FALSE(an::supportsInstructionSet(capabilities, an::InstructionSet::Avx2));
}

TEST(CpuCapabilitiesTest, TheHighestSupportedSetIsTheHighestOneSupported)
{
    const an::CpuCapabilities capabilities = an::cpuCapabilities();

    // Nothing above it may be supported, which is what makes it usable as the default choice.
    for (std::size_t index = static_cast<std::size_t>(capabilities.highestSupported) + 1u;
         index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet higher = static_cast<an::InstructionSet>(index);
        if (higher == an::InstructionSet::Neon || higher == an::InstructionSet::Automatic)
        {
            continue;
        }
        EXPECT_FALSE(an::supportsInstructionSet(capabilities, higher))
            << "the host supports " << an::instructionSetName(higher) << ", which is above what it reported";
    }
}

TEST(CpuCapabilitiesTest, EveryInstructionSetHasItsOwnName)
{
    std::vector<std::string> names;
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const char* const name = an::instructionSetName(static_cast<an::InstructionSet>(index));
        ASSERT_NE(name, nullptr);
        EXPECT_STRNE(name, "UnknownInstructionSet") << "value " << index << " has no name";
        names.emplace_back(name);
    }

    std::sort(names.begin(), names.end());
    EXPECT_EQ(std::unique(names.begin(), names.end()), names.end()) << "two instruction sets share a name";

    EXPECT_STREQ(an::instructionSetName(static_cast<an::InstructionSet>(an::kInstructionSetCount + 5u)),
                 "UnknownInstructionSet");
}

TEST(CpuCapabilitiesTest, DetectionIsSafeToRaceOn)
{
    // The one property a lazily computed constant has to have. Sixteen threads ask at once, and all
    // of them must come back with the same bytes.
    //
    // Nothing asks on this thread first. That ordering is the whole case: a detection written as a
    // flag beside a value - the shape this file exists to refuse - initialises safely whenever one
    // thread has already been through it, so a reference taken here beforehand would leave sixteen
    // readers agreeing about an answer that was computed before any of them started, and the case
    // would pass over the defect it is named after. Proved rather than reasoned: with the immutable
    // static replaced by that flag, this case passes and ThreadSanitizer reports the race, and it
    // reports nothing at all when the reference is taken first.
    //
    // The gate is the second half of the same point. Threads that are merely started do not arrive
    // together - the first can be finished before the last is created - so they wait on one flag
    // and are released into the call at once.
    constexpr std::uint32_t kThreads = 16u;

    std::atomic<bool> released{false};
    std::vector<an::CpuCapabilities> observed(kThreads);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (std::uint32_t index = 0u; index < kThreads; ++index)
    {
        threads.emplace_back([&released, &observed, index]() {
            while (!released.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            observed[index] = an::cpuCapabilities();
        });
    }
    released.store(true, std::memory_order_release);
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    // Taken afterwards, so the sixteen are the ones that raced for it.
    const an::CpuCapabilities reference = an::cpuCapabilities();
    for (std::uint32_t index = 0u; index < kThreads; ++index)
    {
        EXPECT_EQ(std::memcmp(&observed[index], &reference, sizeof(an::CpuCapabilities)), 0)
            << "thread " << index << " saw a different processor";
    }
}

TEST(CpuCapabilitiesTest, AutomaticIsNotSomethingAHostSupports)
{
    // It is a request, not a capability, and a dispatch table that treated it as one would resolve
    // it against itself.
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    EXPECT_FALSE(an::supportsInstructionSet(capabilities, an::InstructionSet::Automatic));
    EXPECT_NE(capabilities.highestSupported, an::InstructionSet::Automatic);
}

// ---------------------------------------------------------------------------------------------
// Selection: what an operation is going to run on.
// ---------------------------------------------------------------------------------------------

TEST(BackendDispatchTest, AnUnconstrainedOperationGetsSomethingItCanRun)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionOptions options;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
    ASSERT_TRUE(selected.hasValue());

    EXPECT_EQ(selected->backend, an::ComputeBackend::Cpu);
    EXPECT_NE(selected->instructionSet, an::InstructionSet::Automatic);
    EXPECT_TRUE(an::supportsInstructionSet(an::cpuCapabilities(), selected->instructionSet));
    EXPECT_TRUE(an::instructionSetIsCompiledIn(selected->instructionSet));
}

TEST(BackendDispatchTest, TheReferenceBackendCanBeForcedAndIsThenWhatRuns)
{
    // The case a conformance run depends on: asking for the oracle must produce the oracle, not the
    // fastest thing that happens to be compiled in.
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Reference;
    options.backendFallback = an::BackendFallback::Forbidden;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
    ASSERT_TRUE(selected.hasValue());
    EXPECT_EQ(selected->backend, an::ComputeBackend::Reference);
    EXPECT_EQ(selected->instructionSet, an::InstructionSet::Scalar);
}

TEST(BackendDispatchTest, TheReferenceBackendIgnoresAnUnsupportedInstructionSetRequest)
{
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Reference;
    options.instructionSet = an::InstructionSet::Neon;
    options.backendFallback = an::BackendFallback::Forbidden;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    EXPECT_EQ(execution->instructionSet(), an::InstructionSet::Scalar);

    const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
    ASSERT_TRUE(selected.hasValue());
    EXPECT_EQ(selected->backend, an::ComputeBackend::Reference);
    EXPECT_EQ(selected->instructionSet, an::InstructionSet::Scalar);
}

TEST(BackendDispatchTest, EveryInstructionSetThisBuildCompiledAndThisHostRunsCanBeForced)
{
    an::EngineContext engine = makeEngineContext();
    const an::CpuCapabilities capabilities = an::cpuCapabilities();

    std::size_t forceable = 0u;
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet wanted = static_cast<an::InstructionSet>(index);
        if (!an::supportsInstructionSet(capabilities, wanted) || !an::instructionSetIsCompiledIn(wanted))
        {
            continue;
        }
        ++forceable;

        an::ExecutionOptions options;
        options.instructionSet = wanted;
        options.backendFallback = an::BackendFallback::Forbidden;
        const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue()) << an::instructionSetName(wanted);

        const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
        ASSERT_TRUE(selected.hasValue()) << an::instructionSetName(wanted);
        EXPECT_EQ(selected->instructionSet, wanted) << "asked for " << an::instructionSetName(wanted) << " and got "
                                                    << an::instructionSetName(selected->instructionSet);
    }

    // The scalar reference is always one of them, so a build that reported nothing forceable would
    // be a build with no oracle to compare an optimised kernel against.
    EXPECT_GE(forceable, 1u);
    EXPECT_TRUE(an::instructionSetIsCompiledIn(an::InstructionSet::Scalar));
    EXPECT_FALSE(an::instructionSetIsCompiledIn(an::InstructionSet::Automatic));
}

namespace
{

/// An instruction set that cannot be forced here, because this host cannot run it or this build does
/// not contain it.
///
/// @return the set, or `an::InstructionSet::Automatic` when every one of them can be forced.
[[nodiscard]] an::InstructionSet findUnforceableInstructionSet()
{
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(index);
        if (candidate == an::InstructionSet::Automatic)
        {
            continue;
        }
        if (!an::supportsInstructionSet(capabilities, candidate) || !an::instructionSetIsCompiledIn(candidate))
        {
            return candidate;
        }
    }
    return an::InstructionSet::Automatic;
}

} // namespace

TEST(BackendDispatchTest, AnInstructionSetThatCannotBeForcedIsRefusedWhenNoFallbackIsPermitted)
{
    an::EngineContext engine = makeEngineContext();

    const an::InstructionSet unsupported = findUnforceableInstructionSet();
    if (unsupported == an::InstructionSet::Automatic)
    {
        GTEST_SKIP() << "every instruction set this build knows about can be forced here";
    }

    an::ExecutionOptions options;
    options.instructionSet = unsupported;
    options.backendFallback = an::BackendFallback::Forbidden;
    const an::Result<an::ExecutionContext> refused = an::ExecutionContext::create(engine, options);

    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::UnsupportedBackend);

    const an::StatusContextField* const named = refused.status().findContext(an::ContextKey::Parameter);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->text, an::instructionSetName(unsupported));
}

TEST(BackendDispatchTest, AnInstructionSetThatCannotBeForcedIsNarrowedWhenFallbackIsPermitted)
{
    an::EngineContext engine = makeEngineContext();
    const an::CpuCapabilities capabilities = an::cpuCapabilities();

    const an::InstructionSet unsupported = findUnforceableInstructionSet();
    if (unsupported == an::InstructionSet::Automatic)
    {
        GTEST_SKIP() << "every instruction set this build knows about can be forced here";
    }

    an::ExecutionOptions options;
    options.instructionSet = unsupported;
    options.backendFallback = an::BackendFallback::Allowed;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    // Narrowed to unconstrained rather than to a claim about what will run, which is what the
    // backend request does with the same policy.
    EXPECT_EQ(execution->requestedInstructionSet(), unsupported);
    EXPECT_EQ(execution->instructionSet(), an::InstructionSet::Automatic);

    const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
    ASSERT_TRUE(selected.hasValue());
    EXPECT_TRUE(an::supportsInstructionSet(capabilities, selected->instructionSet));
    EXPECT_TRUE(an::instructionSetIsCompiledIn(selected->instructionSet));
}

TEST(BackendDispatchTest, TheGpuBackendIsRefusedWhenThisBuildDoesNotHaveIt)
{
    if (an::engineCapabilities().gpuBackend)
    {
        GTEST_SKIP() << "this build contains the GPU backend";
    }

    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Gpu;
    options.backendFallback = an::BackendFallback::Forbidden;
    const an::Result<an::ExecutionContext> refused = an::ExecutionContext::create(engine, options);

    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::UnsupportedBackend);
}

TEST(BackendDispatchTest, AnExecutionThatHoldsNothingIsRefused)
{
    const an::ExecutionContext empty;
    const an::Result<an::BackendSelection> refused = an::selectBackend(empty);

    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::NotInitialized);
}

TEST(BackendDispatchTest, WhatRanIsReportedRatherThanWhatWasIntended)
{
    // The report comes from the dispatch table's own entry, so a table wired to the wrong
    // implementation is visible here rather than only in a wrong pixel much later.
    an::EngineContext engine = makeEngineContext();

    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Reference;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
    ASSERT_TRUE(selected.hasValue());
    EXPECT_EQ(selected->instructionSet, an::instructionSetOfDispatchedCode(*execution));
}

TEST(BackendDispatchTest, TheOnlyCompiledInstructionSetsAreTheOnesKernelsAreWrittenFor)
{
    // The engine writes vector kernels for the packaged floor and for one rung above it, and the
    // vector abstraction is told to compile those two targets and no others. Every rung below the
    // floor is therefore deliberately absent: a build that reported one would be carrying code
    // nothing dispatches to, which is object size the instruction-set budget is meant to notice.
    EXPECT_TRUE(an::instructionSetIsCompiledIn(an::InstructionSet::Scalar));
    EXPECT_FALSE(an::instructionSetIsCompiledIn(an::InstructionSet::Automatic));
    EXPECT_FALSE(an::instructionSetIsCompiledIn(an::InstructionSet::Sse2));
    EXPECT_FALSE(an::instructionSetIsCompiledIn(an::InstructionSet::Sse42));
    EXPECT_FALSE(an::instructionSetIsCompiledIn(an::InstructionSet::Avx));
}

TEST(BackendDispatchTest, AClaimToHaveCompiledAnInstructionSetIsAnswerableByTheCodeItself)
{
    // "This build contains kernels for that set" and "the code those kernels are in was compiled
    // for that set" are two different claims, and the second is the one that matters. They are
    // read from different places on purpose - one from the dispatch table's existence, the other
    // from a function compiled inside the per-target code - so a table wired to the wrong
    // implementation disagrees with itself here rather than in a wrong sample much later.
    an::EngineContext engine = makeEngineContext();
    const an::CpuCapabilities capabilities = an::cpuCapabilities();

    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(index);
        if (candidate == an::InstructionSet::Automatic || !an::instructionSetIsCompiledIn(candidate) ||
            !an::supportsInstructionSet(capabilities, candidate))
        {
            continue;
        }

        an::ExecutionOptions options;
        options.backend =
            candidate == an::InstructionSet::Scalar ? an::ComputeBackend::Reference : an::ComputeBackend::Cpu;
        options.instructionSet = candidate == an::InstructionSet::Scalar ? an::InstructionSet::Automatic : candidate;
        options.backendFallback = an::BackendFallback::Forbidden;
        const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
        ASSERT_TRUE(execution.hasValue()) << an::instructionSetName(candidate);
        EXPECT_EQ(an::instructionSetOfDispatchedCode(*execution), candidate)
            << "the code behind " << an::instructionSetName(candidate) << " reports "
            << an::instructionSetName(an::instructionSetOfDispatchedCode(*execution));
    }
}

TEST(BackendDispatchTest, TheSelectionIsAnnouncedToTheDiagnosticSink)
{
    an::EngineContext engine = makeEngineContext();
    RecordingDiagnosticSink sink;

    an::ExecutionOptions options;
    options.diagnosticSink = &sink;
    options.backend = an::ComputeBackend::Reference;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
    ASSERT_TRUE(selected.hasValue());

    EXPECT_GE(sink.count(), 1u);
    EXPECT_TRUE(sink.mentions(an::computeBackendName(selected->backend)));
    EXPECT_TRUE(sink.mentions(an::instructionSetName(selected->instructionSet)));
    EXPECT_EQ(sink.lastOperationId(), execution->operationId());
    EXPECT_EQ(sink.lastSeverity(), an::DiagnosticSeverity::Debug);
}

TEST(BackendDispatchTest, AnOperationWithNoSinkSaysNothingRatherThanFailing)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionOptions options;
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());

    EXPECT_TRUE(an::selectBackend(*execution).hasValue());
}

TEST(BackendDispatchTest, SelectingFromSeveralThreadsAtOnceGivesOneAnswer)
{
    // Forced selection must not race with the immutable initialisation behind it, which is the
    // concurrency requirement this pair of headers carries.
    constexpr std::uint32_t kThreads = 16u;
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = kThreads;
    an::Result<an::EngineContext> created = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(created.hasValue());
    an::EngineContext engine = std::move(*created);

    std::vector<an::InstructionSet> observed(kThreads, an::InstructionSet::Automatic);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (std::uint32_t index = 0u; index < kThreads; ++index)
    {
        threads.emplace_back([&engine, &observed, index]() {
            an::ExecutionOptions options;
            options.maxConcurrency = 1u;
            const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
            EXPECT_TRUE(execution.hasValue());
            if (!execution.hasValue())
            {
                return;
            }
            const an::Result<an::BackendSelection> selected = an::selectBackend(*execution);
            EXPECT_TRUE(selected.hasValue());
            if (selected.hasValue())
            {
                observed[index] = selected->instructionSet;
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    for (std::uint32_t index = 1u; index < kThreads; ++index)
    {
        EXPECT_EQ(observed[index], observed[0]) << "thread " << index << " selected a different implementation";
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The differential sweep: every subject, every generated case, every compiled backend.
//
// The per-family suites in this directory each compare one operation on the cases their author
// chose. This runs the generator in `conformance/ConformanceHarness.h` over all of them at once and
// checks three separate claims, so that a failure says which one broke:
//
//   - a forced backend is the backend that ran, and it says so;
//   - the reference backend gives the same answer at one thread and at several; and
//   - every compiled backend agrees with the reference under its family numeric contract.
//
// The seed is fixed so that a failure reproduces: the case that failed is named by its subject and
// index in the message, and re-running the binary regenerates exactly that case.

#include "conformance/ConformanceHarness.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// The seed the sweep is generated from.
///
/// Fixed, because a harness whose cases changed between runs would report a failure nobody could
/// reproduce and would silently stop covering whatever it covered yesterday. Widening the sweep is
/// deliberately an edit to these two constants rather than an environment variable: a run whose
/// coverage depended on the shell it started in would make two runs of one binary incomparable, and
/// the evidence a run records has to say what it covered.
constexpr std::uint64_t kSweepSeed = 0x00C0FFEE12345678ull;

/// How many cases each subject is swept over.
constexpr std::uint32_t kSweepCases = 128u;

[[nodiscard]] an::EngineContext conformanceEngine()
{
    an::EngineContextOptions options;
    // Wide enough that the reference run, the threaded reference run and a candidate run can each
    // hold their own grant without the third being refused for want of budget.
    options.maxConcurrency = 16u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

} // namespace

TEST(BackendConformanceTest, EveryCompiledBackendAgreesWithTheReferenceOverTheGeneratedSweep)
{
    const std::vector<conformance::Target> candidates = conformance::candidateTargets();
    if (candidates.empty())
    {
        GTEST_SKIP() << "this build and host have no compiled backend to compare against the reference";
    }
    an::EngineContext engine = conformanceEngine();
    ASSERT_TRUE(engine.valid());

    const conformance::Target reference = conformance::referenceTarget();
    constexpr std::uint64_t seed = kSweepSeed;
    constexpr std::uint32_t cases = kSweepCases;
    std::uint64_t compared = 0u;
    std::uint64_t refused = 0u;

    for (const conformance::Subject& subject : conformance::subjects())
    {
        for (std::uint32_t index = 0u; index < cases; ++index)
        {
            const conformance::Case generated = conformance::generateCase(subject, seed, index);
            const conformance::Outcome expected = subject.run(engine, reference, generated);
            ASSERT_TRUE(expected.setupFailure.empty())
                << conformance::describeCase(subject, generated) << ": " << expected.setupFailure;
            ASSERT_EQ(expected.dispatched, an::InstructionSet::Scalar)
                << conformance::describeCase(subject, generated) << ": the reference backend dispatched elsewhere";
            refused += expected.ran ? 0u : 1u;

            for (const conformance::Target& candidate : candidates)
            {
                const conformance::Outcome actual = subject.run(engine, candidate, generated);
                ASSERT_TRUE(actual.setupFailure.empty()) << conformance::describeCase(subject, generated) << " on "
                                                         << candidate.name << ": " << actual.setupFailure;
                // No hidden fallback: what ran is read out of the selected table own entry, so a
                // table wired to the wrong implementation fails here rather than in a wrong pixel.
                ASSERT_EQ(actual.dispatched, candidate.instructionSet)
                    << conformance::describeCase(subject, generated) << ": " << candidate.name << " dispatched to "
                    << an::instructionSetName(actual.dispatched);
                const std::string difference = conformance::compareOutcomes(subject, generated, expected, actual);
                EXPECT_TRUE(difference.empty())
                    << conformance::describeCase(subject, generated) << " on " << candidate.name << ": " << difference;
                ++compared;
            }
        }
    }

    EXPECT_GT(compared, 0u);
    std::cout << "compared " << compared << " backend runs over " << conformance::subjects().size() << " subjects and "
              << cases << " cases each; " << refused << " case(s) were refused identically by both sides\n";
}

TEST(BackendConformanceTest, TheReferenceGivesTheSameAnswerAtEveryThreadCount)
{
    an::EngineContext engine = conformanceEngine();
    ASSERT_TRUE(engine.valid());

    const conformance::Target reference = conformance::referenceTarget();
    constexpr std::uint64_t seed = kSweepSeed;
    constexpr std::uint32_t cases = kSweepCases;

    for (const conformance::Subject& subject : conformance::subjects())
    {
        for (std::uint32_t index = 0u; index < cases; ++index)
        {
            conformance::Case generated = conformance::generateCase(subject, seed, index);
            const std::uint32_t threads = generated.threads;
            generated.threads = 1u;
            const conformance::Outcome single = subject.run(engine, reference, generated);
            ASSERT_TRUE(single.setupFailure.empty())
                << conformance::describeCase(subject, generated) << ": " << single.setupFailure;
            generated.threads = threads;
            const conformance::Outcome many = subject.run(engine, reference, generated);
            ASSERT_TRUE(many.setupFailure.empty())
                << conformance::describeCase(subject, generated) << ": " << many.setupFailure;

            // Bit for bit, whatever the family: one binary, one input, one target and one set of
            // options produce one answer, and how many threads carried it is not an input.
            ASSERT_EQ(single.ran, many.ran) << conformance::describeCase(subject, generated);
            ASSERT_EQ(single.code, many.code) << conformance::describeCase(subject, generated);
            ASSERT_EQ(single.bytes, many.bytes)
                << conformance::describeCase(subject, generated) << ": the written samples depend on the thread count";
            ASSERT_EQ(single.values, many.values)
                << conformance::describeCase(subject, generated) << ": a reported number depends on the thread count";
        }
    }
}

TEST(BackendConformanceTest, ForcingAnAbsentInstructionSetIsRefusedRatherThanSubstituted)
{
    an::EngineContext engine = conformanceEngine();
    ASSERT_TRUE(engine.valid());

    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    std::uint32_t refusals = 0u;
    for (std::uint8_t rung = static_cast<std::uint8_t>(an::InstructionSet::Sse2);
         rung <= static_cast<std::uint8_t>(an::InstructionSet::Avx512Skylake); ++rung)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(rung);
        if (an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            continue;
        }
        an::ExecutionOptions options;
        options.backend = an::ComputeBackend::Cpu;
        options.instructionSet = candidate;
        options.backendFallback = an::BackendFallback::Forbidden;
        const an::Result<an::ExecutionContext> refused = an::ExecutionContext::create(engine, options);
        ASSERT_FALSE(refused.hasValue()) << an::instructionSetName(candidate) << " was accepted by a build or host "
                                         << "that cannot serve it";
        EXPECT_EQ(refused.status().code(), an::ErrorCode::UnsupportedBackend) << an::instructionSetName(candidate);
        ++refusals;
    }

    // Every host this engine runs on has at least one rung it cannot serve, because the ladder names
    // sets no build compiles a kernel for. A run that refused nothing would mean the check above
    // never executed.
    EXPECT_GT(refusals, 0u);
}

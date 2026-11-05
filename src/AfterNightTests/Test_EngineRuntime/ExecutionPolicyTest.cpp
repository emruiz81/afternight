// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Runtime/ExecutionPolicy.h.
//
// Three closed enumerations and the names beside them. What is worth testing is the same thing that
// was worth testing about the error vocabulary: that the sets are closed, that every value has a
// name, that no two share one, and that a value from outside the enumeration is reported as unknown
// rather than read off the end of a table. The name tables carry no default label, so a value added
// without a name fails the build - this is the runtime half of that guard, for the compilers that
// warn about an unhandled enumerator and for the ones that do not.

#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <set>
#include <string>

namespace
{

TEST(ExecutionPolicyTest, EveryComputeBackendHasItsOwnName)
{
    // Walked over the whole range rather than over a list written out here, so a backend added
    // without a name fails this case on every compiler - not only on the ones that report an
    // unhandled switch label. That is the same guard the error vocabulary has, for the same reason:
    // one of the two compilers this engine is built with does not warn at the level it is built at.
    static_assert(an::kComputeBackendCount == static_cast<std::size_t>(an::ComputeBackend::Gpu) + 1u);

    std::set<std::string> names;
    for (std::size_t value = 0u; value < an::kComputeBackendCount; ++value)
    {
        const char* name = an::computeBackendName(static_cast<an::ComputeBackend>(value));
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0u);
        EXPECT_STRNE(name, "UnknownComputeBackend");
        names.insert(name);
    }
    EXPECT_EQ(names.size(), an::kComputeBackendCount);

    // And the guard on `Gpu` staying last: a backend added after it would be named here, and this
    // fails; one added before it moves `Gpu` up and the count follows on its own.
    EXPECT_STREQ(an::computeBackendName(static_cast<an::ComputeBackend>(an::kComputeBackendCount)),
                 "UnknownComputeBackend");
}

TEST(ExecutionPolicyTest, EveryBackendFallbackAndReductionRuleHasItsOwnName)
{
    EXPECT_STRNE(an::backendFallbackName(an::BackendFallback::Allowed),
                 an::backendFallbackName(an::BackendFallback::Forbidden));
    EXPECT_STRNE(an::reductionDeterminismName(an::ReductionDeterminism::DecisionFeeding),
                 an::reductionDeterminismName(an::ReductionDeterminism::All));

    EXPECT_GT(std::strlen(an::backendFallbackName(an::BackendFallback::Allowed)), 0u);
    EXPECT_GT(std::strlen(an::reductionDeterminismName(an::ReductionDeterminism::All)), 0u);
}

TEST(ExecutionPolicyTest, AValueFromOutsideAnEnumerationIsReportedAsUnknown)
{
    // The runtime half of the guard the missing default label gives at compile time. A value cast in
    // from outside is a different thing from an enumerator nobody named, and it says so rather than
    // indexing past the end of a table.
    EXPECT_STREQ(an::computeBackendName(static_cast<an::ComputeBackend>(200)), "UnknownComputeBackend");
    EXPECT_STREQ(an::backendFallbackName(static_cast<an::BackendFallback>(200)), "UnknownBackendFallback");
    EXPECT_STREQ(an::reductionDeterminismName(static_cast<an::ReductionDeterminism>(200)),
                 "UnknownReductionDeterminism");
}

TEST(ExecutionPolicyTest, TheBackendsNameTheLayersTheEngineActuallyHas)
{
    // Not decoration: these four are the choices the physical structure offers - the scalar
    // reference kernels a conformance run compares against, the compiled CPU backend, the optional
    // GPU module, and letting the engine choose. Instruction-set selection lives inside the CPU
    // backend and is not one of these.
    EXPECT_STREQ(an::computeBackendName(an::ComputeBackend::Automatic), "Automatic");
    EXPECT_STREQ(an::computeBackendName(an::ComputeBackend::Reference), "Reference");
    EXPECT_STREQ(an::computeBackendName(an::ComputeBackend::Cpu), "Cpu");
    EXPECT_STREQ(an::computeBackendName(an::ComputeBackend::Gpu), "Gpu");
}

} // namespace

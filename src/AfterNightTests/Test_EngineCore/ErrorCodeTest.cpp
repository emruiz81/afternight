// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/ErrorCode.h.
//
// The vocabulary is a table, and the useful tests are the ones a table gets wrong: a code with no
// entry, two codes sharing a name, a category that moved, and - the one that actually bites - a
// code appended after the enumerator the count is derived from, which would leave it outside every
// table this suite walks. That last one is caught by asking what lies just past the end.

#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{

// Every code in the enumeration, walked by value. The enumeration is contiguous from zero, which is
// what makes this total rather than a list somebody has to remember to extend.
std::vector<an::ErrorCode> allErrorCodes()
{
    std::vector<an::ErrorCode> codes;
    for (std::size_t value = 0u; value < an::kErrorCodeCount; ++value)
    {
        codes.push_back(static_cast<an::ErrorCode>(value));
    }
    return codes;
}

TEST(ErrorCodeTest, TheCountCoversTheWholeEnumerationAndNothingBeyondIt)
{
    // The guard on `Internal` staying last. A code added after it would be named here, and this
    // fails; a code added before it moves `Internal` up and the count follows on its own.
    EXPECT_STREQ(an::errorCodeName(static_cast<an::ErrorCode>(an::kErrorCodeCount)), "UnknownErrorCode");
    EXPECT_STREQ(an::errorCodeDescription(static_cast<an::ErrorCode>(an::kErrorCodeCount)),
                 "an error code this build does not know");
    EXPECT_EQ(an::errorCategory(static_cast<an::ErrorCode>(an::kErrorCodeCount)), an::ErrorCategory::Internal);
    EXPECT_GT(an::kErrorCodeCount, 1u);
}

TEST(ErrorCodeTest, EveryCodeHasItsOwnNameAndADescription)
{
    std::set<std::string> names;
    for (const an::ErrorCode code : allErrorCodes())
    {
        const char* name = an::errorCodeName(code);
        const char* description = an::errorCodeDescription(code);
        ASSERT_NE(name, nullptr);
        ASSERT_NE(description, nullptr);
        EXPECT_STRNE(name, "UnknownErrorCode") << "code " << static_cast<int>(code) << " has no name";
        EXPECT_GT(std::strlen(description), 0u);
        EXPECT_TRUE(names.insert(name).second) << name << " names two codes";
    }
    EXPECT_EQ(names.size(), an::kErrorCodeCount);
}

TEST(ErrorCodeTest, OnlyOkIsOutsideEveryFailureCategory)
{
    for (const an::ErrorCode code : allErrorCodes())
    {
        const bool none = an::errorCategory(code) == an::ErrorCategory::None;
        EXPECT_EQ(none, code == an::ErrorCode::Ok) << an::errorCodeName(code);
    }
}

TEST(ErrorCodeTest, CancellationIsExactlyTheTwoWaysACallerStopsAnOperation)
{
    // A cancelled run and one that ran out of its deadline are separate codes and one category, so
    // a report can tell them apart and a log can dismiss both without a call site remembering
    // either name.
    for (const an::ErrorCode code : allErrorCodes())
    {
        const bool expected = code == an::ErrorCode::Cancelled || code == an::ErrorCode::DeadlineExceeded;
        EXPECT_EQ(an::isCancellation(code), expected) << an::errorCodeName(code);
    }
    EXPECT_NE(an::ErrorCode::Cancelled, an::ErrorCode::DeadlineExceeded);
}

TEST(ErrorCodeTest, TheBudgetAndAllocationFailuresAreTheSameKindAndDifferentCodes)
{
    // A caller answers a budget failure by tiling and an allocation failure by giving up, so they
    // must not be one code; both are resource conditions, so they must be one category.
    EXPECT_EQ(an::errorCategory(an::ErrorCode::MemoryBudgetExceeded), an::ErrorCategory::Resource);
    EXPECT_EQ(an::errorCategory(an::ErrorCode::AllocationFailed), an::ErrorCategory::Resource);
    EXPECT_EQ(an::errorCategory(an::ErrorCode::LimitExceeded), an::ErrorCategory::Resource);
    EXPECT_NE(an::ErrorCode::MemoryBudgetExceeded, an::ErrorCode::AllocationFailed);
}

TEST(ErrorCodeTest, TheCategoryIsAvailableAtCompileTime)
{
    static_assert(an::errorCategory(an::ErrorCode::Ok) == an::ErrorCategory::None);
    static_assert(an::errorCategory(an::ErrorCode::InvalidArgument) == an::ErrorCategory::Argument);
    static_assert(an::errorCategory(an::ErrorCode::Aliasing) == an::ErrorCategory::Argument);
    static_assert(an::isCancellation(an::ErrorCode::Cancelled));
    static_assert(!an::isCancellation(an::ErrorCode::InvalidData));
    static_assert(an::kErrorCodeCount == static_cast<std::size_t>(an::ErrorCode::Internal) + 1u);
    EXPECT_EQ(an::errorCategory(an::ErrorCode::Internal), an::ErrorCategory::Internal);
}

TEST(ErrorCodeTest, EveryCategoryAndDomainIsNamed)
{
    const an::ErrorCategory categories[] = {
        an::ErrorCategory::None,       an::ErrorCategory::Argument, an::ErrorCategory::State,
        an::ErrorCategory::Capability, an::ErrorCategory::Resource, an::ErrorCategory::Cancellation,
        an::ErrorCategory::Data,       an::ErrorCategory::External, an::ErrorCategory::Internal};
    std::set<std::string> categoryNames;
    for (const an::ErrorCategory category : categories)
    {
        categoryNames.insert(an::errorCategoryName(category));
    }
    EXPECT_EQ(categoryNames.size(), sizeof(categories) / sizeof(categories[0]));
    EXPECT_EQ(categoryNames.count("UnknownErrorCategory"), 0u);

    const an::ErrorDomain domains[] = {
        an::ErrorDomain::Unspecified, an::ErrorDomain::Core,       an::ErrorDomain::Memory, an::ErrorDomain::Runtime,
        an::ErrorDomain::Image,       an::ErrorDomain::Algorithms, an::ErrorDomain::Io,     an::ErrorDomain::Interop};
    std::set<std::string> domainNames;
    for (const an::ErrorDomain domain : domains)
    {
        domainNames.insert(an::errorDomainName(domain));
    }
    EXPECT_EQ(domainNames.size(), sizeof(domains) / sizeof(domains[0]));
    EXPECT_EQ(domainNames.count("UnknownErrorDomain"), 0u);
}

TEST(ErrorCodeTest, AValueFromOutsideTheEnumerationIsReportedAsUnknownRatherThanGuessed)
{
    EXPECT_STREQ(an::errorCategoryName(static_cast<an::ErrorCategory>(200)), "UnknownErrorCategory");
    EXPECT_STREQ(an::errorDomainName(static_cast<an::ErrorDomain>(200)), "UnknownErrorDomain");
    EXPECT_STREQ(an::errorCodeName(static_cast<an::ErrorCode>(60000)), "UnknownErrorCode");
}

} // namespace

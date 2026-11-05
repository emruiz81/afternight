// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/Result.h.
//
// The property worth the most here is the one that cannot be seen in the type: there are exactly
// two states, and no construction produces a third. A result holding no value while reporting
// success would be read as a value by everything downstream, so the constructor that could produce
// one is tested to refuse.
//
// The rest is the split between the two ways of reading a value - unchecked and free, or checked
// and throwing - and proof that a move-only value passes through intact, which is what makes the
// type usable for the owning handles the engine will return from every allocation.

#include <AfterNightEngine/Core/Result.h>

#include "AllocationCounter.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

// A value with a member, so `operator->` has something to reach.
struct Frame
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

an::Result<Frame> makeFrame(std::uint32_t width, std::uint32_t height)
{
    if (width == 0u || height == 0u)
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   an::StatusDetail{.message = "a frame needs both dimensions",
                                                    .operation = "makeFrame",
                                                    .domain = an::ErrorDomain::Image});
    }
    return Frame{width, height};
}

TEST(ResultTest, AValueAndAFailureAreTheOnlyTwoStates)
{
    const an::Result<Frame> produced = makeFrame(4096u, 2048u);
    ASSERT_TRUE(produced.hasValue());
    EXPECT_TRUE(static_cast<bool>(produced));
    EXPECT_TRUE(produced.status().ok());
    EXPECT_EQ(produced.code(), an::ErrorCode::Ok);
    EXPECT_EQ(produced->width, 4096u);
    EXPECT_EQ((*produced).height, 2048u);

    const an::Result<Frame> refused = makeFrame(0u, 2048u);
    EXPECT_FALSE(refused.hasValue());
    EXPECT_FALSE(static_cast<bool>(refused));
    EXPECT_TRUE(refused.status().failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_STREQ(refused.status().message(), "a frame needs both dimensions");
    EXPECT_EQ(refused.status().domain(), an::ErrorDomain::Image);
}

TEST(ResultTest, AResultCannotBeBuiltThatHoldsNothingAndClaimsSuccess)
{
    // The third state the type exists to make unrepresentable. Handing a successful status to the
    // failure constructor is a defect, and it is reported as one rather than producing a result
    // that every caller reads as a value it does not have.
    const an::Result<int> empty{an::Status::success()};
    EXPECT_FALSE(empty.hasValue());
    EXPECT_TRUE(empty.status().failed());
    EXPECT_EQ(empty.code(), an::ErrorCode::Internal);
}

TEST(ResultTest, TheSuccessPathIsNoexceptAndAllocatesNothing)
{
    static_assert(std::is_nothrow_constructible_v<an::Result<int>, int>);
    static_assert(std::is_nothrow_constructible_v<an::Result<int>, an::Status>);
    static_assert(std::is_nothrow_move_constructible_v<an::Result<int>>);
    static_assert(std::is_nothrow_destructible_v<an::Result<int>>);

    const an::Result<int> result{7};
    static_assert(noexcept(result.hasValue()));
    static_assert(noexcept(result.status()));
    static_assert(noexcept(result.code()));
    static_assert(noexcept(*result));
    static_assert(noexcept(result.operator->()));
    static_assert(noexcept(static_cast<bool>(result)));

    int observed = 0;
    bool failedHeldAValue = true;
    std::uint64_t allocations = 0u;
    {
        const testsupport::ScopedAllocationCounter counter;
        const an::Result<int> produced{41};
        const an::Result<int> failed{an::Status::failure(an::ErrorCode::Cancelled)};
        observed = *produced;
        failedHeldAValue = failed.hasValue();
        allocations = counter.count();
    }
    EXPECT_EQ(allocations, 0u);
    EXPECT_EQ(observed, 41);
    EXPECT_FALSE(failedHeldAValue);
}

TEST(ResultTest, TheCheckedAccessorThrowsAndCarriesTheFailureWithIt)
{
    const an::Result<Frame> refused = makeFrame(0u, 0u);
    try
    {
        (void)refused.value();
        FAIL() << "reading a result that holds no value must not succeed";
    }
    catch (const an::BadResultAccess& defect)
    {
        // The exception says a value was read wrongly; the status it carries says what the
        // operation actually reported. A crash log needs both.
        EXPECT_STREQ(defect.what(), "a Result was read for a value it does not hold");
        EXPECT_EQ(defect.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_STREQ(defect.status().operation(), "makeFrame");
    }
}

TEST(ResultTest, TheCheckedAccessorReturnsTheValueWhenThereIsOne)
{
    an::Result<Frame> produced = makeFrame(16u, 9u);
    EXPECT_EQ(produced.value().width, 16u);
    produced.value().width = 32u;
    EXPECT_EQ(produced->width, 32u);

    const an::Result<Frame> constant = makeFrame(16u, 9u);
    EXPECT_EQ(constant.value().height, 9u);
}

TEST(ResultTest, AFallbackIsUsedOnlyWhenThereIsNoValue)
{
    EXPECT_EQ(makeFrame(3u, 3u).valueOr(Frame{1u, 1u}).width, 3u);
    EXPECT_EQ(makeFrame(0u, 3u).valueOr(Frame{1u, 1u}).width, 1u);

    const an::Result<int> failed{an::Status::failure(an::ErrorCode::NotImplemented)};
    EXPECT_EQ(failed.valueOr(5), 5);
    const an::Result<int> produced{9};
    EXPECT_EQ(produced.valueOr(5), 9);
}

TEST(ResultTest, AMoveOnlyValuePassesThroughIntact)
{
    // The shape every allocating operation will return: an owning handle that cannot be copied. If
    // the result type could not carry one, every such operation would need an out-parameter.
    static_assert(!std::is_copy_constructible_v<an::Result<std::unique_ptr<int>>>);

    an::Result<std::unique_ptr<int>> owned{std::make_unique<int>(11)};
    ASSERT_TRUE(owned.hasValue());
    EXPECT_EQ(**owned, 11);

    const std::unique_ptr<int> taken = std::move(owned).value();
    ASSERT_NE(taken, nullptr);
    EXPECT_EQ(*taken, 11);
}

TEST(ResultTest, AnExpiringResultMovesItsValueOutRatherThanCopyingIt)
{
    an::Result<std::vector<int>> produced{std::vector<int>{1, 2, 3}};
    const std::vector<int> taken = *std::move(produced);
    EXPECT_EQ(taken.size(), 3u);

    an::Result<std::vector<int>> failed{an::Status::failure(an::ErrorCode::Cancelled)};
    const std::vector<int> fallback = std::move(failed).valueOr(std::vector<int>{9});
    EXPECT_EQ(fallback.size(), 1u);
}

TEST(ResultTest, TheFailureItCarriesKeepsItsCauseChain)
{
    const an::Status cause = an::Status::failure(an::ErrorCode::ExternalFailure, std::string("the library refused"));
    const an::Result<int> refused{
        an::Status::failure(an::ErrorCode::InvalidData, an::StatusDetail{.message = "the frame is unreadable"}, cause)};

    EXPECT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidData);
    ASSERT_TRUE(refused.status().hasCause());
    EXPECT_EQ(refused.status().rootCause().code(), an::ErrorCode::ExternalFailure);
}

} // namespace

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the replaced global allocation functions the rest of this suite measures with.
//
// This is test support rather than engine code, so what is checked here is narrow: every form of
// global allocation this executable can make is answered by the implementation that also releases
// it. A form left to the library while its deallocation is replaced hands one pointer to two
// implementations, which an ordinary build hides - the library's nothrow form forwards to the
// replaced throwing one - and a sanitizer reports at the free, as a mismatch between what allocated
// a region and what returned it. gtest asks for one such allocation while running a suite, so the
// crossing is reached by every run rather than by a case written to reach it.
//
// The operator functions are called directly rather than through a new-expression, because a
// compiler may elide the allocation a new-expression makes and there would then be nothing to
// count.

#include "AllocationCounter.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <new>

namespace
{

constexpr std::size_t kSampleBytes = 40u;

TEST(AllocationCounterTest, CountsOneAllocationPerCallOfThePlainForm)
{
    // The anchor for everything below: one call of the throwing form is one counted allocation,
    // which is what makes an expectation of one meaningful in the cases that follow.
    const testsupport::ScopedAllocationCounter counter;
    void* memory = ::operator new(kSampleBytes);
    const std::uint64_t counted = counter.count();
    ::operator delete(memory);

    EXPECT_EQ(counted, 1u);
}

TEST(AllocationCounterTest, CountsTheNothrowFormAndReleasesItThroughThePlainDelete)
{
    // Memory from the nothrow form is released by the plain deallocation - the nothrow deallocation
    // exists only for a constructor that throws - so this pair is exactly the crossing that has to
    // stay inside one implementation.
    const testsupport::ScopedAllocationCounter counter;
    void* memory = ::operator new(kSampleBytes, std::nothrow);
    const std::uint64_t counted = counter.count();
    ASSERT_NE(memory, nullptr);
    ::operator delete(memory);

    EXPECT_EQ(counted, 1u);
}

TEST(AllocationCounterTest, CountsTheNothrowArrayFormAndReleasesItThroughThePlainArrayDelete)
{
    const testsupport::ScopedAllocationCounter counter;
    void* memory = ::operator new[](kSampleBytes, std::nothrow);
    const std::uint64_t counted = counter.count();
    ASSERT_NE(memory, nullptr);
    ::operator delete[](memory);

    EXPECT_EQ(counted, 1u);
}

TEST(AllocationCounterTest, AZeroByteNothrowRequestReturnsUsableMemoryRatherThanNull)
{
    // A null from the nothrow form means the allocator refused, so a zero-byte request must not
    // produce one: it would be read as an allocation failure that did not happen.
    void* memory = ::operator new(0u, std::nothrow);
    ASSERT_NE(memory, nullptr);
    *static_cast<unsigned char*>(memory) = 0u;
    ::operator delete(memory);
}

} // namespace

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Counting the allocations a piece of code makes, so "allocates nothing" is measured.
//
// The result and error model promises that a successful operation allocates nothing, and that a
// failure carrying only a code allocates nothing either - which is what makes the second one
// reportable when the reason for it is that there was no memory. Neither promise can be tested by
// looking at the type: `noexcept` does not imply it, and an accessor asserting that no detail was
// built is the implementation agreeing with itself.
//
// So this suite replaces the global allocation functions and counts. The counter is always on,
// because a single relaxed increment per allocation is nothing in a test binary and arming it would
// add a state that could be left wrong.
//
// What it can see is exactly the right thing. Replacing these functions in this executable does not
// replace them inside AfterNightEngine.dll, which links its own; so this counts allocations
// made by the header-only code the test compiled - which is where every promise above lives.

#pragma once

#include <cstdint>

namespace testsupport
{

/// Every call to a global allocation function this executable has made since it started.
///
/// Thread-safe. Read through `ScopedAllocationCounter` rather than directly, unless what is wanted
/// really is the absolute number.
///
/// @return the running total.
[[nodiscard]] std::uint64_t totalAllocations() noexcept;

/// Allocations made since this object was constructed.
///
/// Declare one immediately before the statements under test and read `count()` immediately after,
/// so that nothing else - a gtest assertion message, a stream - is inside the measured region.
class ScopedAllocationCounter
{
public:
    /// Records the running total as the baseline for this measurement.
    ScopedAllocationCounter() noexcept : m_baseline(totalAllocations()) {}

    /// How much has been allocated since construction.
    ///
    /// @return the number of global allocation calls made since this object was constructed.
    [[nodiscard]] std::uint64_t count() const noexcept
    {
        return totalAllocations() - m_baseline;
    }

private:
    /// The running total at construction.
    std::uint64_t m_baseline;
};

} // namespace testsupport

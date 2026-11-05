// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The comparison the sweeps rest on has to be shown to bite before any of them means anything: a
// tolerance that lets everything through would make every sweep below a pass by construction.

#include "ParityHarness.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{

using parity_test::compare;
using parity_test::Comparison;

TEST(HarnessBitesTest, ADifferenceBeyondTheAbsoluteToleranceIsCountedAndLocated)
{
    std::vector<float> library(64u, 0.5f);
    std::vector<float> engine = library;
    engine[17] += 1.0e-3f;
    const Comparison found = compare(engine, library, 1.0e-4, 0.0);
    EXPECT_EQ(found.beyond, 1u);
    EXPECT_EQ(found.worst, 17u);
    EXPECT_NEAR(found.maxAbs, 1.0e-3, 1.0e-7);
    EXPECT_EQ(found.count, 64u);
    EXPECT_EQ(compare(engine, library, 2.0e-3, 0.0).beyond, 0u);
}

TEST(HarnessBitesTest, TheRelativeAllowanceScalesWithTheLibrarysMagnitude)
{
    const std::vector<float> library{100.0f, 1.0f};
    const std::vector<float> engine{100.005f, 1.005f};
    const Comparison found = compare(engine, library, 0.0, 1.0e-4);
    EXPECT_EQ(found.beyond, 1u) << "only the small value exceeds a relative allowance of 1e-4";
    EXPECT_EQ(found.worst, 1u);
}

TEST(HarnessBitesTest, ANotANumberOnOneSideOnlyIsBeyondEveryTolerance)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> library{0.25f, nan, 0.5f};
    const std::vector<float> engine{0.25f, nan, nan};
    const Comparison found = compare(engine, library, 1.0e9, 1.0e9);
    EXPECT_EQ(found.beyond, 1u);
    EXPECT_EQ(found.worst, 2u);
    EXPECT_TRUE(std::isinf(found.maxAbs));
}

TEST(HarnessBitesTest, TheSeededFixturesAreTheSameOnEveryRun)
{
    const std::vector<float> first = parity_test::uniformPlane(16u, 9u, 42u);
    const std::vector<float> second = parity_test::uniformPlane(16u, 9u, 42u);
    EXPECT_EQ(first, second);
    const std::vector<float> smooth = parity_test::smoothPlane(64u, 48u, 7u);
    EXPECT_EQ(smooth, parity_test::smoothPlane(64u, 48u, 7u));
    for (const float sample : smooth)
    {
        EXPECT_GE(sample, 0.0f);
        EXPECT_LE(sample, 1.0f);
    }
    EXPECT_NE(smooth, parity_test::smoothPlane(64u, 48u, 7u, 0.5, 0.0)) << "a shifted star field is a different plane";
}

} // namespace

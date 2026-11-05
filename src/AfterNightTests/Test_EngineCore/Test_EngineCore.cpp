// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the AfterNightEngine core suite.
//
// The suite links AfterNightEngine and gtest and nothing else, and it includes only the
// engine's public headers. That is the point: an engine test that reached a second
// first-party library, or a private header, would stop proving the engine is self-contained.
//
// The cases themselves live one file per public header, so the suite grows by addition rather
// than by one file growing without bound.

#include <gtest/gtest.h>

// No Qt and no storage-aware service is involved here, so a plain gtest main is all that is
// needed. The other AfterNight test executables own their entry point the same way.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

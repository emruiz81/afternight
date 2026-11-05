// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the AfterNightEngine kernel-substrate suite.
//
// This suite links the engine and gtest and sees only the public include root. That is a
// deliberate part of what it tests: backend contracts, dispatch keys, and the scalar oracle have
// to be usable without reaching through the engine's private source tree.

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

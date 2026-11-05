// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the AfterNightEngine image-model suite.
//
// The suite links AfterNightEngine and gtest and nothing else, and it includes only the
// engine's public headers. It is separate from the core suite because a failure here is a
// descriptor claiming something about an image that is not true, rather than a scalar computing
// the wrong number - and because the image model grows into storage, views and conversion, which
// would otherwise turn one suite into the file that never stops growing.
//
// The cases themselves live one file per public header.

#include <gtest/gtest.h>

// No Qt and no storage-aware service is involved here, so a plain gtest main is all that is
// needed. The other AfterNight test executables own their entry point the same way.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the AfterNightEngine external-adapter suite.
//
// The suite links AfterNightEngine and gtest and nothing else, and it includes only the
// engine's public headers - which for this domain is the property under test as much as
// the behaviour is. An adapter to OpenCV, FFTW or GSL that needed one of those libraries in a
// header would make every consumer of the engine need it too, so a suite that could not be built
// without them would be evidence that the boundary had already moved.
//
// It is separate from the image suite because a failure here is a bridge to somebody else's memory
// model being wrong about that model, rather than this engine being wrong about an image.
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

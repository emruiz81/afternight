// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Entry point for the AfterNightEngine consumer-shape probes.
//
// This suite is not a test of an operation. It holds the non-shipping probes that use the published
// surface the way a consumer would, each shaped after the consumer it stands for: a multi-stage
// orchestration over in-memory frames, the same workflow over files through every domain a stacking
// run needs, a command-line one-shot, and an asynchronous preview. They exist so that API friction is
// found by using the surface the way a consumer does, and what they find is a change to the published
// surface rather than a note in here.
//
// It includes no consumer header, compiles no consumer source, and links no consumer target.

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

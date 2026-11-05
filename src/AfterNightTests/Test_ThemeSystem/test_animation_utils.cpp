// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// Animation toggle: AnimationUtils unit tests.
#include <gtest/gtest.h>

#include "theme/AnimationUtils.h"

namespace
{

// RAII guard to restore animation state after each test
struct ScopedAnimationState
{
    bool previous;
    explicit ScopedAnimationState() : previous(an::ui::AnimationUtils::animationsEnabled()) {}
    ~ScopedAnimationState()
    {
        an::ui::AnimationUtils::setAnimationsEnabled(previous);
    }
};

} // anonymous namespace

TEST(ThemeSystemAnimationUtils, DefaultEnabled)
{
    ScopedAnimationState guard;
    an::ui::AnimationUtils::setAnimationsEnabled(true);

    EXPECT_TRUE(an::ui::AnimationUtils::animationsEnabled());
}

TEST(ThemeSystemAnimationUtils, CanDisable)
{
    ScopedAnimationState guard;

    an::ui::AnimationUtils::setAnimationsEnabled(false);
    EXPECT_FALSE(an::ui::AnimationUtils::animationsEnabled());

    an::ui::AnimationUtils::setAnimationsEnabled(true);
    EXPECT_TRUE(an::ui::AnimationUtils::animationsEnabled());
}

TEST(ThemeSystemAnimationUtils, DurationReturnsValueWhenEnabled)
{
    ScopedAnimationState guard;
    an::ui::AnimationUtils::setAnimationsEnabled(true);

    EXPECT_EQ(an::ui::AnimationUtils::duration(170), 170);
    EXPECT_EQ(an::ui::AnimationUtils::duration(250), 250);
    EXPECT_EQ(an::ui::AnimationUtils::duration(0), 0);
}

TEST(ThemeSystemAnimationUtils, DurationReturnsZeroWhenDisabled)
{
    ScopedAnimationState guard;
    an::ui::AnimationUtils::setAnimationsEnabled(false);

    EXPECT_EQ(an::ui::AnimationUtils::duration(170), 0);
    EXPECT_EQ(an::ui::AnimationUtils::duration(200), 0);
    EXPECT_EQ(an::ui::AnimationUtils::duration(1000), 0);
}

TEST(ThemeSystemAnimationUtils, DurationZeroAlwaysZero)
{
    ScopedAnimationState guard;

    an::ui::AnimationUtils::setAnimationsEnabled(true);
    EXPECT_EQ(an::ui::AnimationUtils::duration(0), 0);

    an::ui::AnimationUtils::setAnimationsEnabled(false);
    EXPECT_EQ(an::ui::AnimationUtils::duration(0), 0);
}

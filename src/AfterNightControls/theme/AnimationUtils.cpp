// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/AnimationUtils.h"

#include <atomic>

namespace
{

/// Whether the interface animates: the user's Interface setting, read by every widget that animates.
std::atomic<bool> g_animationsEnabled{true};

} // namespace

namespace an::ui
{

bool AnimationUtils::animationsEnabled()
{
    return g_animationsEnabled.load(std::memory_order_relaxed);
}

void AnimationUtils::setAnimationsEnabled(const bool enabled)
{
    g_animationsEnabled.store(enabled, std::memory_order_relaxed);
}

} // namespace an::ui

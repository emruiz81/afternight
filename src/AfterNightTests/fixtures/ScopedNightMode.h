// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Night set for one scope of a suite, and put back as it was when the scope ends.

#include "theme/AnDesignTokens.h"

namespace an::ui::testing
{

/// Sets Night for its scope and resolves the tokens again; Night as it was set before is put back when it goes. Night
/// is in force only under the Observatory profile, which a case puts in force first (`ScopedStyleProfile`).
class ScopedNightMode final
{
public:
    explicit ScopedNightMode(const bool night) : m_previous(nightModeSet())
    {
        setNightMode(night);
    }

    ~ScopedNightMode()
    {
        setNightMode(m_previous);
    }

    ScopedNightMode(const ScopedNightMode&) = delete;
    ScopedNightMode& operator=(const ScopedNightMode&) = delete;
    ScopedNightMode(ScopedNightMode&&) = delete;
    ScopedNightMode& operator=(ScopedNightMode&&) = delete;

private:
    bool m_previous;
};

} // namespace an::ui::testing

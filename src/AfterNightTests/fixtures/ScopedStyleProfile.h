// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A style profile in force for one scope of a suite, and the one before it put back when the scope ends.

#include "theme/AnDesignTokens.h"

namespace an::ui::testing
{

/// Puts a style profile in force for its scope, whatever widgets the process already holds, and resolves the tokens
/// again; the profile in force before it is put back, and the tokens resolved again, when it goes. The application
/// never does this: it sets its profile once, before its first widget (`an::ui::setStyleProfile`). A suite does, to
/// test a profile in a process whose widgets were made under another, and makes its widgets inside the scope.
class ScopedStyleProfile final
{
public:
    explicit ScopedStyleProfile(const StyleProfile profile) : m_previous(styleProfile())
    {
        forceStyleProfile(profile);
    }

    ~ScopedStyleProfile()
    {
        forceStyleProfile(m_previous);
    }

    ScopedStyleProfile(const ScopedStyleProfile&) = delete;
    ScopedStyleProfile& operator=(const ScopedStyleProfile&) = delete;
    ScopedStyleProfile(ScopedStyleProfile&&) = delete;
    ScopedStyleProfile& operator=(ScopedStyleProfile&&) = delete;

private:
    StyleProfile m_previous;
};

} // namespace an::ui::testing

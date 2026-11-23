// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The style profiles, apart from the tokens, so that a shell's descriptor names its profile without reaching the
/// token structures.

namespace an::ui
{

/// The look the tokens resolve to, named by the shell the application starts and set once, before the first widget
/// exists (`setStyleProfile()` in `theme/AnDesignTokens.h`). `Observatory` resolves the surface ladder, the softened
/// tint and the accent tables of `theme/ObservatoryPalette.h`; the user's theme, contrast profile, accent family and
/// surface tint are choices under it. A later shell that draws in a look of its own names a profile of its own here.
enum class StyleProfile
{
    Observatory
};

} // namespace an::ui

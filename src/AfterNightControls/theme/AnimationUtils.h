// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

namespace an::ui
{

/// Global animation toggle and duration helper.
///
/// All animated components should gate their animations through this utility so that
/// the accessibility "Disable animations" preference is respected everywhere.
///
/// Usage in animated components:
///   m_animation->setDuration(AnimationUtils::duration(170)); // 170ms or 0
///   if (!AnimationUtils::animationsEnabled()) { applyInstantly(); return; }
///
/// The switch is the Controls library's own: every library and executable that reads or sets it reaches the one the
/// settings set. (Kept in a header, each module would hold a copy of its own - a shell turning animations off would
/// leave every control animating.)
class AFTERNIGHTCONTROLS_API AnimationUtils
{
public:
    /// Returns true if UI animations are currently enabled (default: true).
    [[nodiscard]] static bool animationsEnabled();

    /// Enable or disable all UI animations globally.
    /// Call on app startup from persisted settings and whenever the user changes
    /// the Interface › Animations preference.
    static void setAnimationsEnabled(bool enabled);

    /// Returns @p standardMs if animations are enabled, or 0 if disabled.
    /// Pass the return value as the QPropertyAnimation/QVariantAnimation duration
    /// so that all animated components immediately respect the global toggle.
    [[nodiscard]] static int duration(const int standardMs)
    {
        return animationsEnabled() ? standardMs : 0;
    }
};

} // namespace an::ui

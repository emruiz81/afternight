// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

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
class AnimationUtils
{
public:
    /// Returns true if UI animations are currently enabled (default: true).
    [[nodiscard]] static bool animationsEnabled()
    {
        return s_enabled;
    }

    /// Enable or disable all UI animations globally.
    /// Call on app startup from persisted settings and whenever the user changes
    /// the Interface › Animations preference.
    static void setAnimationsEnabled(const bool enabled)
    {
        s_enabled = enabled;
    }

    /// Returns @p standardMs if animations are enabled, or 0 if disabled.
    /// Pass the return value as the QPropertyAnimation/QVariantAnimation duration
    /// so that all animated components immediately respect the global toggle.
    [[nodiscard]] static int duration(const int standardMs)
    {
        return s_enabled ? standardMs : 0;
    }

private:
    static inline bool s_enabled = true;
};

} // namespace an::ui

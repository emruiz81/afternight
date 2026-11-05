// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QCborMap>
#include <QString>

#include <optional>
#include <string>
#include <string_view>

#include "theme/AnDesignTokens.h"

namespace an::ui::extensions
{

[[nodiscard]] inline std::optional<ThemePreference> resolveHostThemePreference(const std::string_view theme_name,
                                                                               const QCborMap& tokens)
{
    const auto dark_flag_it = tokens.find(QStringLiteral("is_dark"));
    if (dark_flag_it != tokens.end() && dark_flag_it->isBool())
    {
        return dark_flag_it->toBool() ? ThemePreference::Dark : ThemePreference::Light;
    }

    const QString normalized_theme_name = QString::fromStdString(std::string(theme_name)).trimmed().toLower();
    if (normalized_theme_name.contains(QStringLiteral("dark")))
    {
        return ThemePreference::Dark;
    }
    if (normalized_theme_name.contains(QStringLiteral("light")))
    {
        return ThemePreference::Light;
    }

    return std::nullopt;
}

inline bool applyHostThemeSnapshot(const std::string_view theme_name, const QCborMap& tokens)
{
    const auto resolved_preference = resolveHostThemePreference(theme_name, tokens);
    if (!resolved_preference.has_value())
    {
        return false;
    }

    // Apply contrast, accent, and surface tint settings before the theme
    // preference so that the single refreshTokens() call triggered by
    // setThemePreference() picks them up.
    if (const auto it = tokens.find(QStringLiteral("contrast_profile")); it != tokens.end() && it->isInteger())
    {
        setThemeContrastProfile(static_cast<ThemeContrastProfile>(it->toInteger()));
    }
    if (const auto it = tokens.find(QStringLiteral("accent_family")); it != tokens.end() && it->isInteger())
    {
        setThemeAccentFamily(static_cast<ThemeAccentFamily>(it->toInteger()));
    }
    if (const auto it = tokens.find(QStringLiteral("surface_tint_family")); it != tokens.end() && it->isInteger())
    {
        setThemeSurfaceTintFamily(static_cast<ThemeSurfaceTintFamily>(it->toInteger()));
    }

    setThemePreference(*resolved_preference);
    return true;
}

} // namespace an::ui::extensions
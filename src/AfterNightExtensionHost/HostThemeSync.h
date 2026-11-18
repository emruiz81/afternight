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

/// The number an entry of the theme state holds, when it names an enumerator from 0 to `last`; nothing otherwise.
[[nodiscard]] inline std::optional<int> hostThemeChoice(const QCborMap& tokens, const QString& name, const int last)
{
    const auto it = tokens.find(name);
    if (it == tokens.end() || !it->isInteger())
    {
        return std::nullopt;
    }
    const qint64 value = it->toInteger();
    if (value < 0 || value > last)
    {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

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

    // Apply the choices before the theme preference so that the resolution setThemePreference() makes last reads them
    // all: the contrast profile, the accent, the surface tint and its strength - the preset is the three - the density
    // and Night. The style profile is the one the host was started under, set before its first widget.
    if (const auto value =
            hostThemeChoice(tokens, QStringLiteral("contrast_profile"), static_cast<int>(ThemeContrastProfile::Oled)))
    {
        setThemeContrastProfile(static_cast<ThemeContrastProfile>(*value));
    }
    if (const auto value =
            hostThemeChoice(tokens, QStringLiteral("accent_family"), static_cast<int>(ThemeAccentFamily::Rose)))
    {
        setThemeAccentFamily(static_cast<ThemeAccentFamily>(*value));
    }
    if (const auto value = hostThemeChoice(tokens, QStringLiteral("surface_tint_family"),
                                           static_cast<int>(ThemeSurfaceTintFamily::Amber)))
    {
        setThemeSurfaceTintFamily(static_cast<ThemeSurfaceTintFamily>(*value));
    }
    if (const auto value =
            hostThemeChoice(tokens, QStringLiteral("tint_strength"), static_cast<int>(ThemeTintStrength::Strong)))
    {
        setThemeTintStrength(static_cast<ThemeTintStrength>(*value));
    }
    if (const auto value =
            hostThemeChoice(tokens, QStringLiteral("density"), static_cast<int>(ThemeDensity::Comfortable)))
    {
        setThemeDensity(static_cast<ThemeDensity>(*value));
    }
    if (const auto it = tokens.find(QStringLiteral("night")); it != tokens.end() && it->isBool())
    {
        setNightMode(it->toBool());
    }

    setThemePreference(*resolved_preference);
    return true;
}

} // namespace an::ui::extensions
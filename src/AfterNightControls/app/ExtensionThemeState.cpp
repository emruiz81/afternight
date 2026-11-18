// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionThemeState.h"

#include "theme/AnDesignTokens.h"

#include <QColor>
#include <QFont>

namespace an::ui::extensions
{

namespace
{

[[nodiscard]] QString colorTokenHex(const QColor& color)
{
    return color.name(QColor::HexArgb);
}

[[nodiscard]] QCborMap fontTokenToCborMap(const QFont& font)
{
    return QCborMap{
        {QStringLiteral("family"), font.family()},       {QStringLiteral("point_size"), font.pointSizeF()},
        {QStringLiteral("weight"), font.weight()},       {QStringLiteral("italic"), font.italic()},
        {QStringLiteral("underline"), font.underline()}, {QStringLiteral("fixed_pitch"), font.fixedPitch()}};
}

[[nodiscard]] QCborMap colorsOf(const ColorTokens& colors)
{
    return QCborMap{{QStringLiteral("bgPrimary"), colorTokenHex(colors.bgPrimary)},
                    {QStringLiteral("bgSurface"), colorTokenHex(colors.bgSurface)},
                    {QStringLiteral("bgCard"), colorTokenHex(colors.bgCard)},
                    {QStringLiteral("textPrimary"), colorTokenHex(colors.textPrimary)},
                    {QStringLiteral("textSecondary"), colorTokenHex(colors.textSecondary)},
                    {QStringLiteral("textTertiary"), colorTokenHex(colors.textTertiary)},
                    {QStringLiteral("textOnAccent"), colorTokenHex(colors.textOnAccent)},
                    {QStringLiteral("accentPrimary"), colorTokenHex(colors.accentPrimary)},
                    {QStringLiteral("accentHover"), colorTokenHex(colors.accentHover)},
                    {QStringLiteral("borderPrimary"), colorTokenHex(colors.borderPrimary)},
                    {QStringLiteral("borderSubtle"), colorTokenHex(colors.borderSubtle)},
                    {QStringLiteral("statusSuccess"), colorTokenHex(colors.statusSuccess)},
                    {QStringLiteral("statusWarning"), colorTokenHex(colors.statusWarning)},
                    {QStringLiteral("statusError"), colorTokenHex(colors.statusError)},
                    {QStringLiteral("statusInfo"), colorTokenHex(colors.statusInfo)}};
}

/// The surface ladder, from the window's surface to the highest raised one.
[[nodiscard]] QCborMap layersOf(const LayerTokens& layers)
{
    return QCborMap{{QStringLiteral("surface0"), colorTokenHex(layers.surface0)},
                    {QStringLiteral("surface1"), colorTokenHex(layers.surface1)},
                    {QStringLiteral("surface2"), colorTokenHex(layers.surface2)},
                    {QStringLiteral("surface3"), colorTokenHex(layers.surface3)},
                    {QStringLiteral("surface4"), colorTokenHex(layers.surface4)}};
}

/// The sizes of the interface in the density in force, in logical pixels.
[[nodiscard]] QCborMap metricsOf(const MetricTokens& metrics)
{
    return QCborMap{{QStringLiteral("fontBody"), metrics.fontBody},
                    {QStringLiteral("fontSmall"), metrics.fontSmall},
                    {QStringLiteral("fontCaption"), metrics.fontCaption},
                    {QStringLiteral("control"), metrics.control},
                    {QStringLiteral("controlSmall"), metrics.controlSmall},
                    {QStringLiteral("controlXSmall"), metrics.controlXSmall},
                    {QStringLiteral("controlLarge"), metrics.controlLarge},
                    {QStringLiteral("rowTable"), metrics.rowTable},
                    {QStringLiteral("rowDense"), metrics.rowDense},
                    {QStringLiteral("rowKeyValue"), metrics.rowKeyValue},
                    {QStringLiteral("rowSettings"), metrics.rowSettings},
                    {QStringLiteral("rowPalette"), metrics.rowPalette},
                    {QStringLiteral("barTop"), metrics.barTop},
                    {QStringLiteral("strip"), metrics.strip},
                    {QStringLiteral("statusLine"), metrics.statusLine},
                    {QStringLiteral("rail"), metrics.rail},
                    {QStringLiteral("pad"), metrics.pad},
                    {QStringLiteral("gap"), metrics.gap},
                    {QStringLiteral("sectionHeader"), metrics.sectionHeader},
                    {QStringLiteral("chip"), metrics.chip},
                    {QStringLiteral("tag"), metrics.tag},
                    {QStringLiteral("floatingToolbar"), metrics.floatingToolbar},
                    {QStringLiteral("scrollBar"), metrics.scrollBar}};
}

} // namespace

const std::vector<QString>& extensionThemeStateNames()
{
    static const std::vector<QString> names = {QStringLiteral("is_dark"),
                                               QStringLiteral("style_profile"),
                                               QStringLiteral("contrast_profile"),
                                               QStringLiteral("accent_family"),
                                               QStringLiteral("surface_tint_family"),
                                               QStringLiteral("tint_strength"),
                                               QStringLiteral("preset"),
                                               QStringLiteral("density"),
                                               QStringLiteral("night"),
                                               QStringLiteral("colors"),
                                               QStringLiteral("layers"),
                                               QStringLiteral("metrics"),
                                               QStringLiteral("spacing"),
                                               QStringLiteral("radius"),
                                               QStringLiteral("typography")};
    return names;
}

QCborMap currentExtensionThemeTokens()
{
    const DesignTokens& tokenSet = tokens();
    return QCborMap{{QStringLiteral("is_dark"), isDarkTheme()},
                    {QStringLiteral("style_profile"), styleProfileName(styleProfile())},
                    {QStringLiteral("contrast_profile"), static_cast<int>(themeContrastProfile())},
                    {QStringLiteral("accent_family"), static_cast<int>(themeAccentFamily())},
                    {QStringLiteral("surface_tint_family"), static_cast<int>(themeSurfaceTintFamily())},
                    {QStringLiteral("tint_strength"), static_cast<int>(themeTintStrength())},
                    {QStringLiteral("preset"), static_cast<int>(themePreset())},
                    {QStringLiteral("density"), static_cast<int>(themeDensity())},
                    {QStringLiteral("night"), isNightMode()},
                    {QStringLiteral("colors"), colorsOf(tokenSet.colors)},
                    {QStringLiteral("layers"), layersOf(tokenSet.layers)},
                    {QStringLiteral("metrics"), metricsOf(tokenSet.metrics)},
                    {QStringLiteral("spacing"), QCborMap{{QStringLiteral("xs"), tokenSet.spacing.xs},
                                                         {QStringLiteral("sm"), tokenSet.spacing.sm},
                                                         {QStringLiteral("md"), tokenSet.spacing.md},
                                                         {QStringLiteral("lg"), tokenSet.spacing.lg},
                                                         {QStringLiteral("xl"), tokenSet.spacing.xl}}},
                    {QStringLiteral("radius"), QCborMap{{QStringLiteral("none"), tokenSet.radius.none},
                                                        {QStringLiteral("sm"), tokenSet.radius.sm},
                                                        {QStringLiteral("md"), tokenSet.radius.md},
                                                        {QStringLiteral("lg"), tokenSet.radius.lg},
                                                        {QStringLiteral("xl"), tokenSet.radius.xl}}},
                    {QStringLiteral("typography"),
                     QCborMap{{QStringLiteral("heading1"), fontTokenToCborMap(tokenSet.typography.heading1)},
                              {QStringLiteral("heading2"), fontTokenToCborMap(tokenSet.typography.heading2)},
                              {QStringLiteral("heading3"), fontTokenToCborMap(tokenSet.typography.heading3)},
                              {QStringLiteral("body"), fontTokenToCborMap(tokenSet.typography.body)},
                              {QStringLiteral("caption"), fontTokenToCborMap(tokenSet.typography.caption)},
                              {QStringLiteral("monospace"), fontTokenToCborMap(tokenSet.typography.monospace)}}}};
}

ExtensionThemeSnapshot currentExtensionThemeSnapshot()
{
    ExtensionThemeSnapshot snapshot;
    snapshot.theme_name = isDarkTheme() ? "AfterNight Dark" : "AfterNight Light";
    snapshot.tokens = currentExtensionThemeTokens();
    return snapshot;
}

QString styleProfileName(const StyleProfile profile)
{
    switch (profile)
    {
    case StyleProfile::Classic:
        return QStringLiteral("classic");
    case StyleProfile::Observatory:
        return QStringLiteral("observatory");
    }
    return QStringLiteral("classic");
}

std::optional<StyleProfile> styleProfileNamed(const QString& name)
{
    const QString normalized = name.trimmed().toLower();
    for (const StyleProfile profile : {StyleProfile::Classic, StyleProfile::Observatory})
    {
        if (normalized == styleProfileName(profile))
        {
            return profile;
        }
    }
    return std::nullopt;
}

std::vector<std::string> hostLookArguments(const StyleProfile profile)
{
    return {"--style-profile", styleProfileName(profile).toStdString()};
}

} // namespace an::ui::extensions

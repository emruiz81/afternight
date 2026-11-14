// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/AnDesignTokens.h"

#include "theme/NightImage.h"
#include "theme/ObservatoryPalette.h"

#include <QApplication>
#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QPalette>

#include <AfterNightPlatform/Logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{

QColor withAlpha(QColor color, const int alpha)
{
    color.setAlpha(std::clamp(alpha, 0, 255));
    return color;
}

QColor blendRgb(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  base.alpha());
}

const an::ui::DesignTokens& designTokensForVariant(const an::ui::ThemeVariant themeVariant)
{
    return themeVariant == an::ui::ThemeVariant::Dark ? an::ui::resolvedDarkTokens() : an::ui::resolvedLightTokens();
}

QColor blendRgba(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  lerp(base.alpha(), tint.alpha()));
}

QColor compositeOver(const QColor& foreground, const QColor& background)
{
    const double alpha = std::clamp(static_cast<double>(foreground.alpha()) / 255.0, 0.0, 1.0);
    const auto blendChannel = [alpha](const int fg, const int bg) {
        return static_cast<int>(std::round(fg * alpha + bg * (1.0 - alpha)));
    };

    return QColor(blendChannel(foreground.red(), background.red()),
                  blendChannel(foreground.green(), background.green()),
                  blendChannel(foreground.blue(), background.blue()), 255);
}

double relativeLuminance(const QColor& color)
{
    const auto convert = [](const int channel) {
        const double s = static_cast<double>(channel) / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };

    const double r = convert(color.red());
    const double g = convert(color.green());
    const double b = convert(color.blue());

    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double l1 = relativeLuminance(a);
    const double l2 = relativeLuminance(b);
    const double light = (std::max)(l1, l2);
    const double dark = (std::min)(l1, l2);
    return (light + 0.05) / (dark + 0.05);
}

const char* themeVariantLabel(const bool darkVariant)
{
    return darkVariant ? "dark" : "light";
}

const char* contrastProfileLabel(const an::ui::ThemeContrastProfile profile)
{
    switch (profile)
    {
    case an::ui::ThemeContrastProfile::High:
        return "high";
    case an::ui::ThemeContrastProfile::Balanced:
        return "balanced";
    case an::ui::ThemeContrastProfile::Soft:
        return "soft";
    case an::ui::ThemeContrastProfile::Oled:
        return "oled";
    }

    return "unknown";
}

const char* accentFamilyLabel(const an::ui::ThemeAccentFamily family)
{
    using an::ui::ThemeAccentFamily;

    switch (family)
    {
    case ThemeAccentFamily::Blue:
        return "blue";
    case ThemeAccentFamily::Teal:
        return "teal";
    case ThemeAccentFamily::Amber:
        return "amber";
    case ThemeAccentFamily::Amethyst:
        return "amethyst";
    case ThemeAccentFamily::Rose:
        return "rose";
    }

    return "unknown";
}

const char* surfaceTintFamilyLabel(const an::ui::ThemeSurfaceTintFamily family)
{
    using an::ui::ThemeSurfaceTintFamily;

    switch (family)
    {
    case ThemeSurfaceTintFamily::Neutral:
        return "neutral";
    case ThemeSurfaceTintFamily::Ocean:
        return "ocean";
    case ThemeSurfaceTintFamily::Mint:
        return "mint";
    case ThemeSurfaceTintFamily::Amethyst:
        return "amethyst";
    case ThemeSurfaceTintFamily::Rose:
        return "rose";
    case ThemeSurfaceTintFamily::Amber:
        return "amber";
    }

    return "unknown";
}

const char* styleProfileLabel(const an::ui::StyleProfile profile)
{
    return profile == an::ui::StyleProfile::Observatory ? "observatory" : "classic";
}

const char* densityLabel(const an::ui::ThemeDensity density)
{
    return density == an::ui::ThemeDensity::Comfortable ? "comfortable" : "compact";
}

const char* tintStrengthLabel(const an::ui::ThemeTintStrength strength)
{
    switch (strength)
    {
    case an::ui::ThemeTintStrength::Off:
        return "off";
    case an::ui::ThemeTintStrength::Subtle:
        return "subtle";
    case an::ui::ThemeTintStrength::Balanced:
        return "balanced";
    case an::ui::ThemeTintStrength::Strong:
        return "strong";
    }

    return "unknown";
}

struct ContrastValidationRule
{
    const char* name = "";
    QColor foreground;
    QColor background;
    double minimumRatio = 4.5;
};

void validateTokenContrastRatios(const an::ui::DesignTokens& tokenSet, const an::ui::ThemeChoices& choices)
{
#ifndef NDEBUG
    const bool canLogWarnings = an::platform::isLoggingInitialized();
    const bool darkVariant = choices.theme == an::ui::ThemeVariant::Dark;
    const auto& c = tokenSet.colors;

    // The Observatory profile is held to its own rules, which its suite enumerates for every combination.
    if (choices.profile == an::ui::StyleProfile::Observatory)
    {
        if (!canLogWarnings)
        {
            return;
        }
        for (const an::ui::ContrastFailure& failure : an::ui::observatoryContrastFailures(tokenSet, choices))
        {
            LOG_WARNING("Theme token contrast validation failed [observatory|{}|{}|{}|{}|{}] {}: {:.2f} < {:.2f}",
                        themeVariantLabel(darkVariant), contrastProfileLabel(choices.contrast),
                        accentFamilyLabel(choices.accent), surfaceTintFamilyLabel(choices.tint),
                        tintStrengthLabel(choices.tintStrength), failure.pair.toStdString(), failure.ratio,
                        failure.minimum);
        }
        return;
    }

    const QColor warningBackground = compositeOver(c.statusWarningSoft, c.bgCardSecondary);
    const QColor warningText = blendRgba(c.textPrimary, c.statusWarning, darkVariant ? 0.34 : 0.56);

    const QColor dangerBackground = compositeOver(c.statusErrorSoft, c.bgCardSecondary);
    const QColor dangerText = blendRgba(c.textPrimary, c.statusError, darkVariant ? 0.34 : 0.52);

    const QColor successBackground = compositeOver(c.statusSuccessSoft, c.bgCardSecondary);
    const QColor successText = blendRgba(c.textPrimary, c.statusSuccess, darkVariant ? 0.34 : 0.56);

    const QColor infoBackground = compositeOver(c.statusInfoSoft, c.bgCardSecondary);
    const QColor infoText = blendRgba(c.textPrimary, c.statusInfo, darkVariant ? 0.34 : 0.56);

    const std::array<ContrastValidationRule, 10> checks = {{
        {"text.primary on bg.surface", c.textPrimary, c.bgSurface, 4.5},
        {"text.primary on bg.cardPrimary", c.textPrimary, c.bgCardPrimary, 4.5},
        {"text.secondary on bg.surface", c.textSecondary, c.bgSurface, 3.0},
        {"text.secondary on bg.cardPrimary", c.textSecondary, c.bgCardPrimary, 3.0},
        {"text.accent on bg.surface", c.textAccent, c.bgSurface, 3.0},
        {"text.onAccent on accent.primary", c.textOnAccent, c.accentPrimary, 4.5},
        {"warning.text on warning.background", warningText, warningBackground, 3.0},
        {"danger.text on danger.background", dangerText, dangerBackground, 3.0},
        {"success.text on success.background", successText, successBackground, 3.0},
        {"info.text on info.background", infoText, infoBackground, 3.0},
    }};

    for (const auto& check : checks)
    {
        const QColor effectiveForeground = compositeOver(check.foreground, check.background);
        const double ratio = contrastRatio(effectiveForeground, check.background);
        if (canLogWarnings && ratio + 1e-6 < check.minimumRatio)
        {
            LOG_WARNING("Theme token contrast validation failed [{}|{}|{}|{}] {}: {:.2f} < {:.2f}",
                        themeVariantLabel(darkVariant), contrastProfileLabel(choices.contrast),
                        accentFamilyLabel(choices.accent), surfaceTintFamilyLabel(choices.tint), check.name, ratio,
                        check.minimumRatio);
        }
    }
#else
    (void)tokenSet;
    (void)choices;
#endif
}

bool parseBoolEnvFlag(const char* envName, const bool defaultValue)
{
    if (!envName)
    {
        return defaultValue;
    }

#ifdef _MSC_VER
    char* raw = nullptr;
    size_t rawLen = 0;
    if (_dupenv_s(&raw, &rawLen, envName) != 0 || !raw || !*raw)
    {
        free(raw);
        return defaultValue;
    }
    std::unique_ptr<char, decltype(&free)> rawGuard(raw, &free);
#else
    const char* raw = std::getenv(envName);
    if (!raw || !*raw)
    {
        return defaultValue;
    }
#endif

    std::string normalized(raw);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
    {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
    {
        return false;
    }

    return defaultValue;
}

bool tokenDebugDumpEnabled()
{
    static const bool enabled = parseBoolEnvFlag("AN_THEME_TOKEN_DEBUG_DUMP", false);
    return enabled;
}

std::string colorToDebugString(const QColor& color)
{
    if (!color.isValid())
    {
        return "<invalid>";
    }

    return color.name(QColor::HexArgb).toUpper().toStdString();
}

void appendColorDumpLine(std::string& report, const char* name, const QColor& color)
{
    report += fmt::format("  {:<40} {}\n", name, colorToDebugString(color));
}

void appendIntDumpLine(std::string& report, const char* name, const int value)
{
    report += fmt::format("  {:<40} {}\n", name, value);
}

/// The colour roles the Observatory profile names, in the order the structure declares them, for the reports.
std::array<std::pair<const char*, const QColor*>, 26u> addedColorRoles(const an::ui::ColorTokens& colors)
{
    return {{
        {"colors.bgCanvas", &colors.bgCanvas},
        {"colors.bgWell", &colors.bgWell},
        {"colors.bgSnack", &colors.bgSnack},
        {"colors.textOnSnack", &colors.textOnSnack},
        {"colors.stateLayer", &colors.stateLayer},
        {"colors.scrim", &colors.scrim},
        {"colors.scrimSoft", &colors.scrimSoft},
        {"colors.edge", &colors.edge},
        {"colors.textOnImage", &colors.textOnImage},
        {"colors.textOnImageSecondary", &colors.textOnImageSecondary},
        {"colors.accentOnImage", &colors.accentOnImage},
        {"colors.textOnAccentImage", &colors.textOnAccentImage},
        {"colors.accentContainer", &colors.accentContainer},
        {"colors.textOnAccentContainer", &colors.textOnAccentContainer},
        {"colors.accentTrack", &colors.accentTrack},
        {"colors.accentSoft", &colors.accentSoft},
        {"colors.accentSofter", &colors.accentSofter},
        {"colors.accentLine", &colors.accentLine},
        {"colors.channelRed", &colors.channelRed},
        {"colors.channelGreen", &colors.channelGreen},
        {"colors.channelBlue", &colors.channelBlue},
        {"colors.channelGray", &colors.channelGray},
        {"colors.plotRed", &colors.plotRed},
        {"colors.plotGreen", &colors.plotGreen},
        {"colors.plotBlue", &colors.plotBlue},
        {"colors.plotGray", &colors.plotGray},
    }};
}

void appendColorTokensDump(std::string& report, const an::ui::ColorTokens& colors)
{
    appendColorDumpLine(report, "colors.bgPrimary", colors.bgPrimary);
    appendColorDumpLine(report, "colors.bgSurface", colors.bgSurface);
    appendColorDumpLine(report, "colors.bgCard", colors.bgCard);
    appendColorDumpLine(report, "colors.bgCardPrimary", colors.bgCardPrimary);
    appendColorDumpLine(report, "colors.bgCardSecondary", colors.bgCardSecondary);
    appendColorDumpLine(report, "colors.bgInput", colors.bgInput);
    appendColorDumpLine(report, "colors.bgHover", colors.bgHover);
    appendColorDumpLine(report, "colors.textPrimary", colors.textPrimary);
    appendColorDumpLine(report, "colors.textSecondary", colors.textSecondary);
    appendColorDumpLine(report, "colors.textTertiary", colors.textTertiary);
    appendColorDumpLine(report, "colors.textAccent", colors.textAccent);
    appendColorDumpLine(report, "colors.textOnAccent", colors.textOnAccent);
    appendColorDumpLine(report, "colors.borderPrimary", colors.borderPrimary);
    appendColorDumpLine(report, "colors.borderSubtle", colors.borderSubtle);
    appendColorDumpLine(report, "colors.borderFocus", colors.borderFocus);
    appendColorDumpLine(report, "colors.statusSuccess", colors.statusSuccess);
    appendColorDumpLine(report, "colors.statusWarning", colors.statusWarning);
    appendColorDumpLine(report, "colors.statusError", colors.statusError);
    appendColorDumpLine(report, "colors.statusInfo", colors.statusInfo);
    appendColorDumpLine(report, "colors.statusSuccessSoft", colors.statusSuccessSoft);
    appendColorDumpLine(report, "colors.statusWarningSoft", colors.statusWarningSoft);
    appendColorDumpLine(report, "colors.statusErrorSoft", colors.statusErrorSoft);
    appendColorDumpLine(report, "colors.statusInfoSoft", colors.statusInfoSoft);
    appendColorDumpLine(report, "colors.accentPrimary", colors.accentPrimary);
    appendColorDumpLine(report, "colors.accentHover", colors.accentHover);
    appendColorDumpLine(report, "colors.accentPressed", colors.accentPressed);
    appendColorDumpLine(report, "colors.accentDisabled", colors.accentDisabled);
    appendColorDumpLine(report, "colors.accentPinned", colors.accentPinned);
    appendColorDumpLine(report, "colors.cardSurfaceTint", colors.cardSurfaceTint);
    appendColorDumpLine(report, "colors.cardSurfaceBorderTint", colors.cardSurfaceBorderTint);
    appendColorDumpLine(report, "colors.shadowOverlay", colors.shadowOverlay);
    for (const auto& [name, color] : addedColorRoles(colors))
    {
        appendColorDumpLine(report, name, *color);
    }
}

void appendLayerTokensDump(std::string& report, const an::ui::LayerTokens& layers)
{
    appendColorDumpLine(report, "layers.surface0", layers.surface0);
    appendColorDumpLine(report, "layers.surface1", layers.surface1);
    appendColorDumpLine(report, "layers.surface2", layers.surface2);
    appendColorDumpLine(report, "layers.surface3", layers.surface3);
    appendColorDumpLine(report, "layers.surface4", layers.surface4);
}

void appendMaterialTokensDump(std::string& report, const an::ui::MaterialTokens& materials)
{
    appendColorDumpLine(report, "materials.solidSurface", materials.solidSurface);
    appendColorDumpLine(report, "materials.micaSurface", materials.micaSurface);
    appendColorDumpLine(report, "materials.acrylicSurface", materials.acrylicSurface);
}

void appendSpacingTokensDump(std::string& report, const an::ui::SpacingTokens& spacing)
{
    appendIntDumpLine(report, "spacing.xs", spacing.xs);
    appendIntDumpLine(report, "spacing.sm", spacing.sm);
    appendIntDumpLine(report, "spacing.md", spacing.md);
    appendIntDumpLine(report, "spacing.lg", spacing.lg);
    appendIntDumpLine(report, "spacing.xl", spacing.xl);
}

void appendRadiusTokensDump(std::string& report, const an::ui::RadiusTokens& radius)
{
    appendIntDumpLine(report, "radius.none", radius.none);
    appendIntDumpLine(report, "radius.sm", radius.sm);
    appendIntDumpLine(report, "radius.md", radius.md);
    appendIntDumpLine(report, "radius.lg", radius.lg);
    appendIntDumpLine(report, "radius.xl", radius.xl);
}

void appendTypographyTokensDump(std::string& report, const an::ui::TypographyTokens& typography)
{
    const auto appendFont = [&report](const char* name, const QFont& font) {
        report += fmt::format("  {:<40} family=\"{}\", pointSize={}, weight={}\n", name, font.family().toStdString(),
                              font.pointSize(), static_cast<int>(font.weight()));
    };

    appendFont("typography.heading1", typography.heading1);
    appendFont("typography.heading2", typography.heading2);
    appendFont("typography.heading3", typography.heading3);
    appendFont("typography.body", typography.body);
    appendFont("typography.caption", typography.caption);
    appendFont("typography.monospace", typography.monospace);
}

void appendMetricTokensDump(std::string& report, const an::ui::MetricTokens& metrics)
{
    appendIntDumpLine(report, "metrics.fontBody", metrics.fontBody);
    appendIntDumpLine(report, "metrics.fontSmall", metrics.fontSmall);
    appendIntDumpLine(report, "metrics.fontCaption", metrics.fontCaption);
    appendIntDumpLine(report, "metrics.control", metrics.control);
    appendIntDumpLine(report, "metrics.controlSmall", metrics.controlSmall);
    appendIntDumpLine(report, "metrics.controlXSmall", metrics.controlXSmall);
    appendIntDumpLine(report, "metrics.controlLarge", metrics.controlLarge);
    appendIntDumpLine(report, "metrics.rowTable", metrics.rowTable);
    appendIntDumpLine(report, "metrics.rowDense", metrics.rowDense);
    appendIntDumpLine(report, "metrics.rowKeyValue", metrics.rowKeyValue);
    appendIntDumpLine(report, "metrics.rowSettings", metrics.rowSettings);
    appendIntDumpLine(report, "metrics.rowPalette", metrics.rowPalette);
    appendIntDumpLine(report, "metrics.barTop", metrics.barTop);
    appendIntDumpLine(report, "metrics.strip", metrics.strip);
    appendIntDumpLine(report, "metrics.statusLine", metrics.statusLine);
    appendIntDumpLine(report, "metrics.rail", metrics.rail);
    appendIntDumpLine(report, "metrics.pad", metrics.pad);
    appendIntDumpLine(report, "metrics.gap", metrics.gap);
    appendIntDumpLine(report, "metrics.sectionHeader", metrics.sectionHeader);
    appendIntDumpLine(report, "metrics.chip", metrics.chip);
    appendIntDumpLine(report, "metrics.tag", metrics.tag);
    appendIntDumpLine(report, "metrics.floatingToolbar", metrics.floatingToolbar);
    appendIntDumpLine(report, "metrics.scrollBar", metrics.scrollBar);
}

void appendDesignTokenDumpSection(std::string& report, const char* sectionName, const an::ui::DesignTokens& tokenSet)
{
    report += fmt::format("[{}]\n", sectionName);
    appendColorTokensDump(report, tokenSet.colors);
    appendLayerTokensDump(report, tokenSet.layers);
    appendMaterialTokensDump(report, tokenSet.materials);
    appendSpacingTokensDump(report, tokenSet.spacing);
    appendRadiusTokensDump(report, tokenSet.radius);
    appendTypographyTokensDump(report, tokenSet.typography);
    appendMetricTokensDump(report, tokenSet.metrics);
}

void appendColorDiffLine(std::string& report, const char* name, const QColor& darkColor, const QColor& lightColor,
                         int& diffCount)
{
    if (darkColor == lightColor)
    {
        return;
    }

    ++diffCount;
    report += fmt::format("  {:<40} {} -> {} (dr={:+d}, dg={:+d}, db={:+d}, da={:+d})\n", name,
                          colorToDebugString(darkColor), colorToDebugString(lightColor),
                          lightColor.red() - darkColor.red(), lightColor.green() - darkColor.green(),
                          lightColor.blue() - darkColor.blue(), lightColor.alpha() - darkColor.alpha());
}

void appendIntDiffLine(std::string& report, const char* name, const int darkValue, const int lightValue, int& diffCount)
{
    if (darkValue == lightValue)
    {
        return;
    }

    ++diffCount;
    report += fmt::format("  {:<40} {} -> {} (delta={:+d})\n", name, darkValue, lightValue, lightValue - darkValue);
}

void appendDesignTokenDiffSection(std::string& report, const an::ui::DesignTokens& darkTokenSet,
                                  const an::ui::DesignTokens& lightTokenSet, int& diffCount)
{
    const auto& darkColors = darkTokenSet.colors;
    const auto& lightColors = lightTokenSet.colors;
    appendColorDiffLine(report, "colors.bgPrimary", darkColors.bgPrimary, lightColors.bgPrimary, diffCount);
    appendColorDiffLine(report, "colors.bgSurface", darkColors.bgSurface, lightColors.bgSurface, diffCount);
    appendColorDiffLine(report, "colors.bgCard", darkColors.bgCard, lightColors.bgCard, diffCount);
    appendColorDiffLine(report, "colors.bgCardPrimary", darkColors.bgCardPrimary, lightColors.bgCardPrimary, diffCount);
    appendColorDiffLine(report, "colors.bgCardSecondary", darkColors.bgCardSecondary, lightColors.bgCardSecondary,
                        diffCount);
    appendColorDiffLine(report, "colors.bgInput", darkColors.bgInput, lightColors.bgInput, diffCount);
    appendColorDiffLine(report, "colors.bgHover", darkColors.bgHover, lightColors.bgHover, diffCount);
    appendColorDiffLine(report, "colors.textPrimary", darkColors.textPrimary, lightColors.textPrimary, diffCount);
    appendColorDiffLine(report, "colors.textSecondary", darkColors.textSecondary, lightColors.textSecondary, diffCount);
    appendColorDiffLine(report, "colors.textTertiary", darkColors.textTertiary, lightColors.textTertiary, diffCount);
    appendColorDiffLine(report, "colors.textAccent", darkColors.textAccent, lightColors.textAccent, diffCount);
    appendColorDiffLine(report, "colors.textOnAccent", darkColors.textOnAccent, lightColors.textOnAccent, diffCount);
    appendColorDiffLine(report, "colors.borderPrimary", darkColors.borderPrimary, lightColors.borderPrimary, diffCount);
    appendColorDiffLine(report, "colors.borderSubtle", darkColors.borderSubtle, lightColors.borderSubtle, diffCount);
    appendColorDiffLine(report, "colors.borderFocus", darkColors.borderFocus, lightColors.borderFocus, diffCount);
    appendColorDiffLine(report, "colors.statusSuccess", darkColors.statusSuccess, lightColors.statusSuccess, diffCount);
    appendColorDiffLine(report, "colors.statusWarning", darkColors.statusWarning, lightColors.statusWarning, diffCount);
    appendColorDiffLine(report, "colors.statusError", darkColors.statusError, lightColors.statusError, diffCount);
    appendColorDiffLine(report, "colors.statusInfo", darkColors.statusInfo, lightColors.statusInfo, diffCount);
    appendColorDiffLine(report, "colors.statusSuccessSoft", darkColors.statusSuccessSoft, lightColors.statusSuccessSoft,
                        diffCount);
    appendColorDiffLine(report, "colors.statusWarningSoft", darkColors.statusWarningSoft, lightColors.statusWarningSoft,
                        diffCount);
    appendColorDiffLine(report, "colors.statusErrorSoft", darkColors.statusErrorSoft, lightColors.statusErrorSoft,
                        diffCount);
    appendColorDiffLine(report, "colors.statusInfoSoft", darkColors.statusInfoSoft, lightColors.statusInfoSoft,
                        diffCount);
    appendColorDiffLine(report, "colors.accentPrimary", darkColors.accentPrimary, lightColors.accentPrimary, diffCount);
    appendColorDiffLine(report, "colors.accentHover", darkColors.accentHover, lightColors.accentHover, diffCount);
    appendColorDiffLine(report, "colors.accentPressed", darkColors.accentPressed, lightColors.accentPressed, diffCount);
    appendColorDiffLine(report, "colors.accentDisabled", darkColors.accentDisabled, lightColors.accentDisabled,
                        diffCount);
    appendColorDiffLine(report, "colors.accentPinned", darkColors.accentPinned, lightColors.accentPinned, diffCount);
    appendColorDiffLine(report, "colors.cardSurfaceTint", darkColors.cardSurfaceTint, lightColors.cardSurfaceTint,
                        diffCount);
    appendColorDiffLine(report, "colors.cardSurfaceBorderTint", darkColors.cardSurfaceBorderTint,
                        lightColors.cardSurfaceBorderTint, diffCount);
    appendColorDiffLine(report, "colors.shadowOverlay", darkColors.shadowOverlay, lightColors.shadowOverlay, diffCount);
    const auto darkAdded = addedColorRoles(darkColors);
    const auto lightAdded = addedColorRoles(lightColors);
    for (std::size_t i = 0; i < darkAdded.size(); ++i)
    {
        appendColorDiffLine(report, darkAdded[i].first, *darkAdded[i].second, *lightAdded[i].second, diffCount);
    }

    const auto& darkLayers = darkTokenSet.layers;
    const auto& lightLayers = lightTokenSet.layers;
    appendColorDiffLine(report, "layers.surface0", darkLayers.surface0, lightLayers.surface0, diffCount);
    appendColorDiffLine(report, "layers.surface1", darkLayers.surface1, lightLayers.surface1, diffCount);
    appendColorDiffLine(report, "layers.surface2", darkLayers.surface2, lightLayers.surface2, diffCount);
    appendColorDiffLine(report, "layers.surface3", darkLayers.surface3, lightLayers.surface3, diffCount);
    appendColorDiffLine(report, "layers.surface4", darkLayers.surface4, lightLayers.surface4, diffCount);

    const auto& darkMaterials = darkTokenSet.materials;
    const auto& lightMaterials = lightTokenSet.materials;
    appendColorDiffLine(report, "materials.solidSurface", darkMaterials.solidSurface, lightMaterials.solidSurface,
                        diffCount);
    appendColorDiffLine(report, "materials.micaSurface", darkMaterials.micaSurface, lightMaterials.micaSurface,
                        diffCount);
    appendColorDiffLine(report, "materials.acrylicSurface", darkMaterials.acrylicSurface, lightMaterials.acrylicSurface,
                        diffCount);

    const auto& darkSpacing = darkTokenSet.spacing;
    const auto& lightSpacing = lightTokenSet.spacing;
    appendIntDiffLine(report, "spacing.xs", darkSpacing.xs, lightSpacing.xs, diffCount);
    appendIntDiffLine(report, "spacing.sm", darkSpacing.sm, lightSpacing.sm, diffCount);
    appendIntDiffLine(report, "spacing.md", darkSpacing.md, lightSpacing.md, diffCount);
    appendIntDiffLine(report, "spacing.lg", darkSpacing.lg, lightSpacing.lg, diffCount);
    appendIntDiffLine(report, "spacing.xl", darkSpacing.xl, lightSpacing.xl, diffCount);

    const auto& darkRadius = darkTokenSet.radius;
    const auto& lightRadius = lightTokenSet.radius;
    appendIntDiffLine(report, "radius.none", darkRadius.none, lightRadius.none, diffCount);
    appendIntDiffLine(report, "radius.sm", darkRadius.sm, lightRadius.sm, diffCount);
    appendIntDiffLine(report, "radius.md", darkRadius.md, lightRadius.md, diffCount);
    appendIntDiffLine(report, "radius.lg", darkRadius.lg, lightRadius.lg, diffCount);
    appendIntDiffLine(report, "radius.xl", darkRadius.xl, lightRadius.xl, diffCount);
}

std::string buildResolvedTokenDiffReport(const an::ui::DesignTokens& darkTokenSet,
                                         const an::ui::DesignTokens& lightTokenSet)
{
    std::string report;
    report += "[dark_vs_light_diff]\n";
    int diffCount = 0;
    appendDesignTokenDiffSection(report, darkTokenSet, lightTokenSet, diffCount);
    report += fmt::format("  {:<40} {}\n", "diff.total", diffCount);
    return report;
}

std::string buildResolvedTokenDumpReport(const an::ui::DesignTokens& darkTokenSet,
                                         const an::ui::DesignTokens& lightTokenSet, const bool darkThemeActive,
                                         const an::ui::ThemeChoices& choices)
{
    std::string report;
    report += fmt::format(
        "Resolved theme tokens report [active={}, profile={}, contrast={}, accent={}, surfaceTint={}, "
        "tintStrength={}, density={}]\n",
        themeVariantLabel(darkThemeActive), styleProfileLabel(choices.profile), contrastProfileLabel(choices.contrast),
        accentFamilyLabel(choices.accent), surfaceTintFamilyLabel(choices.tint),
        tintStrengthLabel(choices.tintStrength), densityLabel(choices.density));
    appendDesignTokenDumpSection(report, "dark.resolved", darkTokenSet);
    appendDesignTokenDumpSection(report, "light.resolved", lightTokenSet);
    report += buildResolvedTokenDiffReport(darkTokenSet, lightTokenSet);
    return report;
}

QColor semanticColorFromTokens(const an::ui::ThemeVariant themeVariant, const an::ui::SemanticColorRole role)
{
    using an::ui::SemanticColorRole;
    const auto& tokenSet = designTokensForVariant(themeVariant);
    const auto& c = tokenSet.colors;
    const auto& layers = tokenSet.layers;
    const auto& materials = tokenSet.materials;
    const bool dark = (themeVariant == an::ui::ThemeVariant::Dark);

    switch (role)
    {
    case SemanticColorRole::SurfaceWindow:
        return layers.surface0;
    case SemanticColorRole::SurfaceElevated:
        return layers.surface3;
    case SemanticColorRole::SurfaceOverlay:
        return layers.surface2;
    case SemanticColorRole::SurfaceBorder:
        return c.borderPrimary;

    case SemanticColorRole::TextPrimary:
        return c.textPrimary;
    case SemanticColorRole::TextSecondary:
        return c.textSecondary;
    case SemanticColorRole::TextMuted:
        return c.textTertiary;
    case SemanticColorRole::TextOnAccent:
        return c.textOnAccent;

    case SemanticColorRole::AccentPrimary:
        return c.accentPrimary;
    case SemanticColorRole::AccentHover:
        return c.accentHover;
    case SemanticColorRole::AccentSoft:
        return withAlpha(c.accentPrimary, dark ? 46 : 36);

    case SemanticColorRole::HighlightBackground:
        return blendRgba(layers.surface2, c.accentPrimary, dark ? 0.18 : 0.12);
    case SemanticColorRole::HighlightBorder:
        return withAlpha(c.borderFocus, dark ? 208 : 184);
    case SemanticColorRole::CodeBackground:
        return materials.acrylicSurface;

    case SemanticColorRole::WarningBackground:
        return c.statusWarningSoft;
    case SemanticColorRole::WarningBorder:
        return c.statusWarning;
    case SemanticColorRole::WarningText:
        return blendRgba(c.textPrimary, c.statusWarning, dark ? 0.34 : 0.56);
    case SemanticColorRole::DangerBackground:
        return c.statusErrorSoft;
    case SemanticColorRole::DangerBorder:
        return c.statusError;
    case SemanticColorRole::DangerText:
        return blendRgba(c.textPrimary, c.statusError, dark ? 0.34 : 0.52);
    case SemanticColorRole::SuccessBackground:
        return c.statusSuccessSoft;
    case SemanticColorRole::SuccessBorder:
        return c.statusSuccess;
    case SemanticColorRole::SuccessText:
        return blendRgba(c.textPrimary, c.statusSuccess, dark ? 0.34 : 0.56);
    case SemanticColorRole::InfoBackground:
        return c.statusInfoSoft;
    case SemanticColorRole::InfoBorder:
        return c.statusInfo;
    case SemanticColorRole::InfoText:
        return blendRgba(c.textPrimary, c.statusInfo, dark ? 0.34 : 0.56);

    case SemanticColorRole::StatusSuccess:
        return c.statusSuccess;
    case SemanticColorRole::StatusWarning:
        return c.statusWarning;
    case SemanticColorRole::StatusError:
        return c.statusError;
    case SemanticColorRole::StatusPending:
        return c.textTertiary;
    case SemanticColorRole::StatusActive:
        return c.accentPrimary;
    case SemanticColorRole::StatusMaster:
        return c.accentPinned;
    }

    return c.textPrimary;
}

QColor adjustSaturationValue(const QColor& color, const double saturationScale, const double valueScale)
{
    float hue = 0.0F;
    float saturation = 0.0F;
    float value = 0.0F;
    float alpha = 0.0F;
    color.getHsvF(&hue, &saturation, &value, &alpha);

    saturation = static_cast<float>(std::clamp(saturation * saturationScale, 0.0, 1.0));
    value = static_cast<float>(std::clamp(value * valueScale, 0.0, 1.0));

    QColor adjusted;
    adjusted.setHsvF(hue, saturation, value, alpha);
    return adjusted;
}

namespace Palette
{
constexpr QColor Blue300(147, 197, 253);
constexpr QColor Blue400(96, 165, 250);
constexpr QColor Blue500(59, 130, 246);
constexpr QColor Blue600(37, 99, 235);
constexpr QColor Blue700(29, 78, 216);
constexpr QColor Blue800(30, 64, 175);

constexpr QColor Teal300(94, 234, 212);
constexpr QColor Teal400(45, 212, 191);
constexpr QColor Teal500(20, 184, 166);
constexpr QColor Teal600(13, 148, 136);
constexpr QColor Teal700(15, 118, 110);
constexpr QColor Teal800(17, 94, 89);

constexpr QColor Amber200(253, 230, 138);
constexpr QColor Amber300(252, 211, 77);
constexpr QColor Amber400(251, 191, 36);
constexpr QColor Amber500(245, 158, 11);
constexpr QColor Amber600(217, 119, 6);
constexpr QColor Amber700(180, 83, 9);
constexpr QColor Amber800(146, 64, 14);

constexpr QColor Amethyst300(196, 181, 253);
constexpr QColor Amethyst400(167, 139, 250);
constexpr QColor Amethyst500(139, 92, 246);
constexpr QColor Amethyst600(124, 58, 237);
constexpr QColor Amethyst700(109, 40, 217);
constexpr QColor Amethyst800(91, 33, 182);

constexpr QColor Rose300(253, 164, 175);
constexpr QColor Rose400(251, 113, 133);
constexpr QColor Rose500(244, 63, 94);
constexpr QColor Rose600(225, 29, 72);
constexpr QColor Rose700(190, 18, 60);
constexpr QColor Rose800(159, 18, 57);
} // namespace Palette

struct AccentFamilyPalette
{
    QColor textAccent;
    QColor borderFocus;
    QColor statusInfo;
    QColor accentPrimary;
    QColor accentHover;
    QColor accentPressed;
    int accentDisabledAlpha = 96;
};

AccentFamilyPalette accentFamilyPalette(const bool darkVariant, const an::ui::ThemeAccentFamily family)
{
    using an::ui::ThemeAccentFamily;

    if (darkVariant)
    {
        switch (family)
        {
        case ThemeAccentFamily::Amethyst:
            return {
                Palette::Amethyst300, // textAccent: #C4B5FD
                withAlpha(Palette::Amethyst500, 206), // borderFocus: #8B5CF6
                Palette::Amethyst500, // statusInfo: #8B5CF6
                Palette::Amethyst500, // accentPrimary: #8B5CF6
                Palette::Amethyst400, // accentHover: #A78BFA
                Palette::Amethyst600, // accentPressed: #7C3AED
                90,
            };
        case ThemeAccentFamily::Rose:
            return {
                Palette::Rose300, // textAccent: #FDA4AF
                withAlpha(Palette::Rose500, 206), // borderFocus: #F43F5E
                Palette::Rose500, // statusInfo: #F43F5E
                Palette::Rose500, // accentPrimary: #F43F5E
                Palette::Rose400, // accentHover: #FB7185
                Palette::Rose600, // accentPressed: #E11D48
                90,
            };
        case ThemeAccentFamily::Teal:
            return {
                Palette::Teal300, // textAccent: #5EEAD4
                withAlpha(Palette::Teal400, 206), // borderFocus: #2DD4BF
                Palette::Teal400, // statusInfo: #2DD4BF
                Palette::Teal400, // accentPrimary: #2DD4BF
                Palette::Teal300, // accentHover: #5EEAD4
                Palette::Teal500, // accentPressed: #14B8A6
                90,
            };
        case ThemeAccentFamily::Amber:
            return {
                Palette::Amber200, // textAccent: #FDE68A
                withAlpha(Palette::Amber400, 206), // borderFocus: #FBBF24
                Palette::Amber400, // statusInfo: #FBBF24
                Palette::Amber400, // accentPrimary: #FBBF24
                Palette::Amber300, // accentHover: #FCD34D
                Palette::Amber500, // accentPressed: #F59E0B
                90,
            };
        case ThemeAccentFamily::Blue:
        default:
            return {
                Palette::Blue300, // textAccent: #93C5FD
                withAlpha(Palette::Blue400, 206), // borderFocus: #60A5FA
                Palette::Blue400, // statusInfo: #60A5FA
                Palette::Blue400, // accentPrimary: #60A5FA
                Palette::Blue300, // accentHover: #93C5FD
                Palette::Blue500, // accentPressed: #3B82F6
                90,
            };
        }
    }

    switch (family)
    {
    case ThemeAccentFamily::Amethyst:
        return {
            Palette::Amethyst700, // textAccent: #6D28D9
            withAlpha(Palette::Amethyst600, 205), // borderFocus: #7C3AED
            Palette::Amethyst600, // statusInfo: #7C3AED
            Palette::Amethyst600, // accentPrimary: #7C3AED
            Palette::Amethyst700, // accentHover: #6D28D9
            Palette::Amethyst800, // accentPressed: #5B21B6
            96,
        };
    case ThemeAccentFamily::Rose:
        return {
            Palette::Rose700, // textAccent: #BE123C
            withAlpha(Palette::Rose600, 205), // borderFocus: #E11D48
            Palette::Rose600, // statusInfo: #E11D48
            Palette::Rose600, // accentPrimary: #E11D48
            Palette::Rose700, // accentHover: #BE123C
            Palette::Rose800, // accentPressed: #9F1239
            96,
        };
    case ThemeAccentFamily::Teal:
        return {
            Palette::Teal700, // textAccent: #0F766E
            withAlpha(Palette::Teal600, 205), // borderFocus: #0D9488
            Palette::Teal600, // statusInfo: #0D9488
            Palette::Teal600, // accentPrimary: #0D9488
            Palette::Teal700, // accentHover: #0F766E
            Palette::Teal800, // accentPressed: #115E59
            96,
        };
    case ThemeAccentFamily::Amber:
        return {
            Palette::Amber700, // textAccent: #B45309
            withAlpha(Palette::Amber600, 205), // borderFocus: #D97706
            Palette::Amber600, // statusInfo: #D97706
            Palette::Amber600, // accentPrimary: #D97706
            Palette::Amber700, // accentHover: #B45309
            Palette::Amber800, // accentPressed: #92400E
            96,
        };
    case ThemeAccentFamily::Blue:
    default:
        return {
            Palette::Blue700, // textAccent: #1D4ED8
            withAlpha(Palette::Blue600, 205), // borderFocus: #2563EB
            Palette::Blue600, // statusInfo: #2563EB
            Palette::Blue600, // accentPrimary: #2563EB
            Palette::Blue700, // accentHover: #1D4ED8
            Palette::Blue800, // accentPressed: #1E40AF
            96,
        };
    }
}

an::ui::DesignTokens applyThemeAccentFamily(const an::ui::DesignTokens& base, const bool darkVariant,
                                            const an::ui::ThemeAccentFamily family)
{
    an::ui::DesignTokens tokenSet = base;
    auto& c = tokenSet.colors;

    const AccentFamilyPalette palette = accentFamilyPalette(darkVariant, family);
    c.textAccent = palette.textAccent;
    c.borderFocus = palette.borderFocus;
    c.statusInfo = palette.statusInfo;
    c.accentPrimary = palette.accentPrimary;
    c.accentHover = palette.accentHover;
    c.accentPressed = palette.accentPressed;
    c.accentDisabled = withAlpha(c.accentPrimary, palette.accentDisabledAlpha);
    return tokenSet;
}

QColor surfaceTintColor(const bool darkVariant, const an::ui::ThemeSurfaceTintFamily family)
{
    using an::ui::ThemeSurfaceTintFamily;

    if (darkVariant)
    {
        switch (family)
        {
        case ThemeSurfaceTintFamily::Ocean:
            return Palette::Blue500; // #3B82F6 (Vibrant Azure)
        case ThemeSurfaceTintFamily::Mint:
            return Palette::Teal500; // #14B8A6 (Mint/Emerald)
        case ThemeSurfaceTintFamily::Amethyst:
            return Palette::Amethyst500; // #8B5CF6 (Electric Violet)
        case ThemeSurfaceTintFamily::Rose:
            return Palette::Rose500; // #F43F5E (Energetic Rose)
        case ThemeSurfaceTintFamily::Amber:
            return Palette::Amber500; // #F59E0B (Warm Amber)
        case ThemeSurfaceTintFamily::Neutral:
        default:
            return QColor();
        }
    }

    switch (family)
    {
    case ThemeSurfaceTintFamily::Ocean:
        return Palette::Blue600; // #2563EB (Royal Blue)
    case ThemeSurfaceTintFamily::Mint:
        return Palette::Teal600; // #0D9488 (Deep Teal)
    case ThemeSurfaceTintFamily::Amethyst:
        return Palette::Amethyst600; // #7C3AED (Deep Violet)
    case ThemeSurfaceTintFamily::Rose:
        return Palette::Rose600; // #E11D48 (Deep Rose)
    case ThemeSurfaceTintFamily::Amber:
        return Palette::Amber600; // #D97706 (Deep Amber)
    case ThemeSurfaceTintFamily::Neutral:
    default:
        return QColor();
    }
}

QColor surfaceTintBorderColor(const bool darkVariant, const an::ui::ThemeSurfaceTintFamily family)
{
    using an::ui::ThemeSurfaceTintFamily;

    // For modern subtle borders, we can derive the border from the primary tint but slightly desaturated
    // or just return the primary tint itself, as AnDesignTokens.cpp will blend it further.
    if (darkVariant)
    {
        switch (family)
        {
        case ThemeSurfaceTintFamily::Ocean:
            return Palette::Blue400; // Lighter Azure
        case ThemeSurfaceTintFamily::Mint:
            return Palette::Teal400; // Lighter Mint
        case ThemeSurfaceTintFamily::Amethyst:
            return Palette::Amethyst400; // Lighter Violet
        case ThemeSurfaceTintFamily::Rose:
            return Palette::Rose400; // Lighter Rose
        case ThemeSurfaceTintFamily::Amber:
            return Palette::Amber400; // Lighter Amber
        case ThemeSurfaceTintFamily::Neutral:
        default:
            return QColor();
        }
    }

    switch (family)
    {
    case ThemeSurfaceTintFamily::Ocean:
        return Palette::Blue500; // Lighter Royal
    case ThemeSurfaceTintFamily::Mint:
        return Palette::Teal500; // Lighter Teal
    case ThemeSurfaceTintFamily::Amethyst:
        return Palette::Amethyst500; // Lighter Violet
    case ThemeSurfaceTintFamily::Rose:
        return Palette::Rose500; // Lighter Rose
    case ThemeSurfaceTintFamily::Amber:
        return Palette::Amber500; // Lighter Amber
    case ThemeSurfaceTintFamily::Neutral:
    default:
        return QColor();
    }
}

an::ui::DesignTokens applyThemeSurfaceTintFamily(const an::ui::DesignTokens& base, const bool darkVariant,
                                                 const an::ui::ThemeSurfaceTintFamily family)
{
    an::ui::DesignTokens tokenSet = base;
    auto& c = tokenSet.colors;
    c.cardSurfaceTint = surfaceTintColor(darkVariant, family);
    c.cardSurfaceBorderTint = surfaceTintBorderColor(darkVariant, family);
    return tokenSet;
}

void refreshLayerAndMaterialMappings(an::ui::DesignTokens& tokenSet)
{
    tokenSet.layers.surface0 = tokenSet.colors.bgPrimary;
    tokenSet.layers.surface1 = tokenSet.colors.bgSurface;
    tokenSet.layers.surface2 = tokenSet.colors.bgCardSecondary;
    tokenSet.layers.surface3 = tokenSet.colors.bgCardPrimary;
    tokenSet.layers.surface4 = tokenSet.colors.bgHover;

    tokenSet.materials.solidSurface = tokenSet.layers.surface1;
    tokenSet.materials.micaSurface = tokenSet.layers.surface0;
    tokenSet.materials.acrylicSurface = tokenSet.colors.bgCard;
}

bool detectNativeBackdropSupport()
{
#if defined(Q_OS_WIN)
    // WinUI 3 native backdrop effects (Mica/Acrylic) require modern DWM support.
    const auto version = QOperatingSystemVersion::current();
    return version >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22000);
#else
    return false;
#endif
}

/// The roles the Observatory profile names, as AfterNight 1.1 paints them, each taken from where 1.1 has it, so that a
/// widget shared by both profiles paints under Classic what it painted. Applied to a resolved set: they follow its
/// accent and its surfaces. `darkWindow` is the dark theme's resolved window, which the viewer's overlays use in both
/// themes.
void applyClassicColorRoles(an::ui::DesignTokens& tokenSet, const bool darkVariant, const QColor& darkWindow)
{
    auto& c = tokenSet.colors;
    const auto& layers = tokenSet.layers;

    // The view's background around an image: the window mixed toward its border (imageViewScrollAreaWindowColor).
    c.bgCanvas = blendRgb(layers.surface0, c.borderPrimary, darkVariant ? 0.22 : 0.52);
    // A plot is drawn on the dark theme's window as it is before any choice, in both themes
    // (HistogramWidget::getBackgroundColor).
    c.bgWell = an::ui::darkTokens().colors.bgPrimary;
    // 1.1 draws one transient message, the histogram's coordinates: the surface at 200 under the primary text
    // (HistogramWidget::refreshOverlayColors).
    c.bgSnack = withAlpha(c.bgSurface, 200);
    c.textOnSnack = c.textPrimary;
    // A hovered control is filled with the hover surface, not laid over (AnProxyStyle).
    c.stateLayer = c.bgHover;
    // The viewer's overlays lie on the dark theme's window at 170 in both themes (imageViewOverlayBackdropColor); a
    // chip on a thumbnail on the theme's window at 170, or 190 in the light theme (ImageThumbnailWidget).
    c.scrim = withAlpha(darkWindow, 170);
    c.scrimSoft = withAlpha(layers.surface0, darkVariant ? 170 : 190);
    // A menu's frame (AnProxyStyle).
    c.edge = c.borderSubtle;
    // The featured card's title and details over its picture (FeaturedFileCard).
    c.textOnImage = QColor(255, 255, 255, 240);
    c.textOnImageSecondary = QColor(255, 255, 255, 190);
    // A selection drawn over the image, and the white edge of the handles drawn on it (ImageViewWidget,
    // DrawingCanvasController).
    c.accentOnImage = c.accentPrimary;
    c.textOnAccentImage = QColor(255, 255, 255);
    // A chosen item's highlight (SemanticColorRole::HighlightBackground), whose text stays the primary text
    // (ImageSelectorControl).
    c.accentContainer = blendRgba(layers.surface2, c.accentPrimary, darkVariant ? 0.18 : 0.12);
    c.textOnAccentContainer = c.textPrimary;
    // A slider's groove (AnProxyStyle).
    c.accentTrack = c.borderSubtle;
    // A selected row (AnProxyStyle), the lighter accent wash (SemanticColorRole::AccentSoft) and the focus line.
    c.accentSoft = withAlpha(c.accentPrimary, darkVariant ? 116 : 84);
    c.accentSofter = withAlpha(c.accentPrimary, darkVariant ? 46 : 36);
    c.accentLine = c.borderFocus;
    // The channel selectors' dots (icons/PNG/red_circle.png and its two siblings); the luminance has no dot, and its
    // legend is drawn in the primary text (QualityDistributionWidget).
    c.channelRed = QColor(255, 0, 0);
    c.channelGreen = QColor(166, 215, 133);
    c.channelBlue = QColor(65, 105, 225);
    c.channelGray = c.textPrimary;
    // The histogram's curves: the pure primaries, and white for a mono image (HistogramWidget).
    c.plotRed = QColor(255, 0, 0);
    c.plotGreen = QColor(0, 255, 0);
    c.plotBlue = QColor(0, 0, 255);
    c.plotGray = QColor(255, 255, 255);
}

an::ui::TypographyTokens createTypographyTokens()
{
    an::ui::TypographyTokens typography;

    typography.body = QFont();
    if (typography.body.pointSize() <= 0)
    {
        typography.body.setPointSize(10);
    }

    typography.heading1 = typography.body;
    typography.heading1.setPointSize(typography.body.pointSize() + 4);
    typography.heading1.setWeight(QFont::Bold);

    typography.heading2 = typography.body;
    typography.heading2.setPointSize(typography.body.pointSize() + 2);
    typography.heading2.setWeight(QFont::DemiBold);

    typography.heading3 = typography.body;
    typography.heading3.setPointSize(typography.body.pointSize() + 1);
    typography.heading3.setWeight(QFont::DemiBold);

    typography.caption = typography.body;
    typography.caption.setPointSize((std::max)(8, typography.body.pointSize() - 1));

    typography.monospace = QFont(QStringLiteral("Cascadia Mono"));
    typography.monospace.setStyleHint(QFont::Monospace);
    typography.monospace.setPointSize(typography.body.pointSize());

    return typography;
}

/// A font's size in logical pixels: the pixel size it states, or its point size at the 96 logical dots per inch Qt 6
/// presents to an application.
int logicalPixels(const QFont& font)
{
    if (font.pixelSize() > 0)
    {
        return font.pixelSize();
    }
    return static_cast<int>(std::lround(font.pointSizeF() * 96.0 / 72.0));
}

/// The sizes AfterNight 1.1 has, where it has one for a role: the height its style gives a field, a combo box, a spin
/// box and a button at least, the thickness of its scroll bars, the sizes of its type scale (it has two: the body and
/// the caption, which is also its small text). A role 1.1 has no size for keeps the Compact density's, so that a widget
/// made for the Observatory profile is sized as it was designed if it is shown under Classic. No Classic painter reads
/// them: the proxy style's Classic answers are its own (Density.ClassicMetricsAreUnchanged holds them).
an::ui::MetricTokens classicMetrics(const an::ui::TypographyTokens& typography)
{
    an::ui::MetricTokens metrics;
    metrics.control = 24;
    metrics.scrollBar = 12;
    metrics.fontBody = logicalPixels(typography.body);
    metrics.fontSmall = logicalPixels(typography.caption);
    metrics.fontCaption = logicalPixels(typography.caption);
    return metrics;
}

an::ui::DesignTokens createDarkTokens()
{
    an::ui::DesignTokens tokenSet;
    auto bg = tokenSet.colors.bg();
    auto text = tokenSet.colors.text();
    auto border = tokenSet.colors.border();
    auto status = tokenSet.colors.status();
    auto accent = tokenSet.colors.accent();
    auto effects = tokenSet.colors.effects();

    // Active palette: Modern Slate & Zinc dark. Historical reference palettes
    // have been removed from the source tree to keep the implementation lean.
    // Archived variants (VS Code neutral, 2026‑02‑27 balanced dark, original
    // blue high‑contrast) are now documented externally if needed.
    bg.primary = QColor(14, 14, 17, 255); // #0E0E11
    bg.surface = QColor(24, 24, 27, 255); // #18181B
    bg.card = QColor(39, 39, 42, 220); // #27272A
    bg.cardPrimary = QColor(39, 39, 42, 240); // #27272A
    bg.cardSecondary = QColor(31, 31, 34, 220); // #1F1F22
    bg.input = QColor(31, 31, 34, 255); // #1F1F22
    bg.hover = QColor(63, 63, 70, 255); // #3F3F46

    text.primary = QColor(244, 244, 245, 255); // #F4F4F5
    text.secondary = QColor(161, 161, 170, 255); // #A1A1AA
    text.tertiary = QColor(113, 113, 122, 255); // #71717A
    text.accent = QColor(147, 197, 253, 255); // #93C5FD
    text.onAccent = QColor(255, 255, 255, 255);

    border.primary = QColor(255, 255, 255, 25); // ~10% white
    border.subtle = QColor(255, 255, 255, 12); // ~5% white
    border.focus = QColor(96, 165, 250, 206); // #60A5FA

    status.success = QColor(52, 211, 153, 255); // #34D399
    status.warning = QColor(251, 191, 36, 255); // #FBBF24
    status.error = QColor(248, 113, 113, 255); // #F87171
    status.info = QColor(96, 165, 250, 255); // #60A5FA

    status.successSoft = withAlpha(status.success, 58);
    status.warningSoft = withAlpha(status.warning, 58);
    status.errorSoft = withAlpha(status.error, 58);
    status.infoSoft = withAlpha(status.info, 58);

    accent.primary = QColor(96, 165, 250, 255); // #60A5FA
    accent.hover = QColor(147, 197, 253, 255); // #93C5FD
    accent.pressed = QColor(59, 130, 246, 255); // #3B82F6
    accent.disabled = QColor(96, 165, 250, 90);
    accent.pinned = QColor(251, 191, 36, 208);
    effects.shadowOverlay = QColor(0, 0, 0, 102); // Deeper shadows

    refreshLayerAndMaterialMappings(tokenSet);

    tokenSet.typography = createTypographyTokens();
    tokenSet.metrics = classicMetrics(tokenSet.typography);

    return tokenSet;
}

an::ui::DesignTokens createLightTokens()
{
    an::ui::DesignTokens tokenSet;
    auto bg = tokenSet.colors.bg();
    auto text = tokenSet.colors.text();
    auto border = tokenSet.colors.border();
    auto status = tokenSet.colors.status();
    auto accent = tokenSet.colors.accent();
    auto effects = tokenSet.colors.effects();

    bg.primary = QColor(250, 250, 250, 255); // #FAFAFA
    bg.surface = QColor(244, 244, 245, 255); // #F4F4F5
    bg.card = QColor(255, 255, 255, 240); // #FFFFFF
    bg.cardPrimary = QColor(255, 255, 255, 248); // #FFFFFF
    bg.cardSecondary = QColor(244, 244, 245, 236); // #F4F4F5
    bg.input = QColor(255, 255, 255, 255); // #FFFFFF
    bg.hover = QColor(228, 228, 231, 255); // #E4E4E7

    text.primary = QColor(24, 24, 27, 255); // #18181B
    text.secondary = QColor(82, 82, 91, 255); // #52525B
    text.tertiary = QColor(161, 161, 170, 255); // #A1A1AA
    text.accent = QColor(29, 78, 216, 255); // #1D4ED8
    text.onAccent = QColor(255, 255, 255, 255);

    border.primary = QColor(212, 212, 216, 220); // #D4D4D8
    border.subtle = QColor(228, 228, 231, 180); // #E4E4E7
    border.focus = QColor(37, 99, 235, 205); // #2563EB

    status.success = QColor(22, 163, 74, 255); // #16A34A
    status.warning = QColor(217, 119, 6, 255); // #D97706
    status.error = QColor(220, 38, 38, 255); // #DC2626
    status.info = QColor(37, 99, 235, 255); // #2563EB

    status.successSoft = withAlpha(status.success, 44);
    status.warningSoft = withAlpha(status.warning, 44);
    // Red is perceptually more saturated, so errorSoft uses a slightly
    // lower alpha (40 vs 44) to keep visual weight comparable to the
    // other status-soft backgrounds.
    status.errorSoft = withAlpha(status.error, 40);
    status.infoSoft = withAlpha(status.info, 44);

    accent.primary = QColor(37, 99, 235, 255); // #2563EB
    accent.hover = QColor(29, 78, 216, 255); // #1D4ED8
    accent.pressed = QColor(30, 64, 175, 255); // #1E40AF
    accent.disabled = QColor(37, 99, 235, 96);
    accent.pinned = QColor(217, 119, 6, 220); // #D97706
    effects.shadowOverlay = QColor(0, 0, 0, 25);

    tokenSet.layers.surface0 = tokenSet.colors.bgPrimary;
    tokenSet.layers.surface1 = tokenSet.colors.bgSurface;
    tokenSet.layers.surface2 = tokenSet.colors.bgCardSecondary;
    tokenSet.layers.surface3 = tokenSet.colors.bgCardPrimary;
    tokenSet.layers.surface4 = tokenSet.colors.bgHover;

    tokenSet.materials.solidSurface = tokenSet.layers.surface1;
    tokenSet.materials.micaSurface = tokenSet.layers.surface0;
    tokenSet.materials.acrylicSurface = tokenSet.colors.bgCard;

    tokenSet.typography = createTypographyTokens();
    tokenSet.metrics = classicMetrics(tokenSet.typography);

    return tokenSet;
}

bool detectDarkThemeFromApplication()
{
    if (!QGuiApplication::instance())
    {
        return true;
    }

    return QGuiApplication::palette().color(QPalette::Window).value() < 128;
}

bool shouldUseNativeBackdropMaterials(const an::ui::MaterialBackdropMode mode, const bool nativeSupported)
{
    if (!nativeSupported || mode == an::ui::MaterialBackdropMode::ForceFallback)
    {
        return false;
    }

    return mode == an::ui::MaterialBackdropMode::Auto || mode == an::ui::MaterialBackdropMode::PreferNative;
}

an::ui::DesignTokens applyThemeContrastProfile(const an::ui::DesignTokens& base, const bool darkVariant,
                                               const an::ui::ThemeContrastProfile profile)
{
    an::ui::DesignTokens tokenSet = base;

    const QColor bgTarget = darkVariant ? QColor(0, 0, 0) : QColor(255, 255, 255);
    const QColor fgTarget = darkVariant ? QColor(255, 255, 255) : QColor(0, 0, 0);

    auto& c = tokenSet.colors;
    if (profile == an::ui::ThemeContrastProfile::High)
    {
        c.bgPrimary = blendRgb(c.bgPrimary, bgTarget, 0.205);
        c.bgSurface = blendRgb(c.bgSurface, bgTarget, 0.172);
        c.bgCard = blendRgb(c.bgCard, bgTarget, 0.152);
        c.bgCardPrimary = blendRgb(c.bgCardPrimary, bgTarget, 0.142);
        c.bgCardSecondary = blendRgb(c.bgCardSecondary, bgTarget, 0.162);
        c.bgInput = blendRgb(c.bgInput, bgTarget, 0.132);
        c.bgHover = blendRgb(c.bgHover, bgTarget, 0.102);

        c.textPrimary = blendRgb(c.textPrimary, fgTarget, 0.115);
        c.textSecondary = blendRgb(c.textSecondary, fgTarget, 0.135);
        c.textTertiary = blendRgb(c.textTertiary, fgTarget, 0.125);
        c.textAccent = adjustSaturationValue(c.textAccent, 1.17, darkVariant ? 1.06 : 0.94);

        c.borderPrimary = blendRgb(c.borderPrimary, fgTarget, 0.18);
        c.borderSubtle = blendRgb(c.borderSubtle, fgTarget, 0.145);
        c.borderFocus = adjustSaturationValue(c.borderFocus, 1.17, darkVariant ? 1.07 : 0.93);
        c.statusInfo = adjustSaturationValue(c.statusInfo, 1.17, darkVariant ? 1.07 : 0.93);

        c.accentPrimary = adjustSaturationValue(c.accentPrimary, 1.24, darkVariant ? 1.08 : 0.92);
        c.accentHover = adjustSaturationValue(c.accentHover, 1.20, darkVariant ? 1.10 : 0.90);
        c.accentPressed = adjustSaturationValue(c.accentPressed, 1.21, darkVariant ? 1.05 : 0.95);
        c.accentDisabled = withAlpha(c.accentPrimary, c.accentDisabled.alpha());
    }
    else if (profile == an::ui::ThemeContrastProfile::Soft)
    {
        c.bgPrimary = blendRgb(c.bgPrimary, fgTarget, 0.030);
        c.bgSurface = blendRgb(c.bgSurface, fgTarget, 0.026);
        c.bgCard = blendRgb(c.bgCard, fgTarget, 0.023);
        c.bgCardPrimary = blendRgb(c.bgCardPrimary, fgTarget, 0.023);
        c.bgCardSecondary = blendRgb(c.bgCardSecondary, fgTarget, 0.026);
        c.bgInput = blendRgb(c.bgInput, fgTarget, 0.023);
        c.bgHover = blendRgb(c.bgHover, fgTarget, 0.019);

        c.textPrimary = blendRgb(c.textPrimary, c.bgSurface, 0.024);
        c.textSecondary = blendRgb(c.textSecondary, c.bgSurface, 0.05);
        c.textTertiary = blendRgb(c.textTertiary, c.bgSurface, 0.07);
        c.textAccent = adjustSaturationValue(c.textAccent, 0.94, darkVariant ? 0.995 : 1.005);

        c.borderPrimary = blendRgb(c.borderPrimary, c.bgSurface, 0.11);
        c.borderSubtle = blendRgb(c.borderSubtle, c.bgSurface, 0.13);
        c.borderFocus = adjustSaturationValue(c.borderFocus, 0.95, 1.00);
        c.statusInfo = adjustSaturationValue(c.statusInfo, 0.95, 1.00);

        c.accentPrimary = adjustSaturationValue(c.accentPrimary, 0.90, darkVariant ? 0.99 : 1.01);
        c.accentHover = adjustSaturationValue(c.accentHover, 0.90, darkVariant ? 1.01 : 0.99);
        c.accentPressed = adjustSaturationValue(c.accentPressed, 0.92, darkVariant ? 0.96 : 1.04);
        c.accentDisabled = withAlpha(c.accentPrimary, c.accentDisabled.alpha());
    }
    else if (profile == an::ui::ThemeContrastProfile::Oled)
    {
        if (!darkVariant)
        {
            // OLED is a dark-only concept (pure black for AMOLED pixel-off).
            // In light mode, silently fall back to the Balanced profile.
            return applyThemeContrastProfile(base, darkVariant, an::ui::ThemeContrastProfile::Balanced);
        }

        c.bgPrimary = QColor(0, 0, 0, 255);
        c.bgSurface = QColor(0, 0, 0, 255);
        c.bgCard = QColor(12, 12, 12, 220);
        c.bgCardPrimary = QColor(14, 14, 14, 240);
        c.bgCardSecondary = QColor(8, 8, 8, 220);
        c.bgInput = QColor(8, 8, 8, 255);
        c.bgHover = QColor(32, 32, 32, 255);

        c.textPrimary = QColor(255, 255, 255, 255);
        c.textSecondary = QColor(180, 180, 180, 255);
        c.textTertiary = QColor(120, 120, 120, 255);
        c.textAccent = adjustSaturationValue(c.textAccent, 1.10, 1.05);

        c.borderPrimary = QColor(255, 255, 255, 40);
        c.borderSubtle = QColor(255, 255, 255, 20);

        c.borderFocus = adjustSaturationValue(c.borderFocus, 1.10, 1.05);
        c.statusSuccess = adjustSaturationValue(c.statusSuccess, 1.10, 1.05);
        c.statusWarning = adjustSaturationValue(c.statusWarning, 1.10, 1.05);
        c.statusError = adjustSaturationValue(c.statusError, 1.10, 1.05);
        c.statusInfo = adjustSaturationValue(c.statusInfo, 1.10, 1.05);
        c.accentPrimary = adjustSaturationValue(c.accentPrimary, 1.15, 1.05);
        c.accentHover = adjustSaturationValue(c.accentHover, 1.15, 1.05);
        c.accentPressed = adjustSaturationValue(c.accentPressed, 1.15, 1.00);
        c.accentDisabled = withAlpha(c.accentPrimary, c.accentDisabled.alpha());
    }
    else
    {
        c.bgPrimary = blendRgb(c.bgPrimary, bgTarget, 0.010);
        c.bgSurface = blendRgb(c.bgSurface, bgTarget, 0.008);
        c.bgCard = blendRgb(c.bgCard, bgTarget, 0.007);
        c.bgCardPrimary = blendRgb(c.bgCardPrimary, bgTarget, 0.006);
        c.bgCardSecondary = blendRgb(c.bgCardSecondary, bgTarget, 0.008);
        c.bgInput = blendRgb(c.bgInput, bgTarget, 0.006);
        c.bgHover = blendRgb(c.bgHover, bgTarget, 0.005);

        c.textPrimary = blendRgb(c.textPrimary, fgTarget, 0.020);
        c.textSecondary = blendRgb(c.textSecondary, fgTarget, 0.024);
        c.textTertiary = blendRgb(c.textTertiary, fgTarget, 0.022);
        c.textAccent = adjustSaturationValue(c.textAccent, 1.03, darkVariant ? 1.01 : 0.99);

        c.borderPrimary = blendRgb(c.borderPrimary, fgTarget, 0.045);
        c.borderSubtle = blendRgb(c.borderSubtle, fgTarget, 0.035);
        c.borderFocus = adjustSaturationValue(c.borderFocus, 1.03, darkVariant ? 1.01 : 0.99);
        c.statusInfo = adjustSaturationValue(c.statusInfo, 1.03, darkVariant ? 1.01 : 0.99);

        c.accentPrimary = adjustSaturationValue(c.accentPrimary, 1.04, darkVariant ? 1.01 : 0.99);
        c.accentHover = adjustSaturationValue(c.accentHover, 1.03, darkVariant ? 1.02 : 0.98);
        c.accentPressed = adjustSaturationValue(c.accentPressed, 1.03, 1.00);
        c.accentDisabled = withAlpha(c.accentPrimary, c.accentDisabled.alpha());
    }

    // Recompute soft-status variants so they stay in sync with any
    // saturation/value adjustments applied to the base status colors above.
    c.statusSuccessSoft = withAlpha(c.statusSuccess, c.statusSuccessSoft.alpha());
    c.statusWarningSoft = withAlpha(c.statusWarning, c.statusWarningSoft.alpha());
    c.statusErrorSoft = withAlpha(c.statusError, c.statusErrorSoft.alpha());
    c.statusInfoSoft = withAlpha(c.statusInfo, c.statusInfoSoft.alpha());

    // Keep card tints profile-aware so soft/balanced/high stay visually
    // ordered while remaining subtle.
    double tintSaturationScale = 0.82;
    double tintValueScale = 0.76;
    double tintBgBlendRatio = 0.08;
    double borderTintSaturationScale = 0.80;
    double borderTintValueScale = 0.78;
    double borderBlendRatio = 0.12;

    switch (profile)
    {
    case an::ui::ThemeContrastProfile::High:
        tintSaturationScale = 0.90;
        tintValueScale = darkVariant ? 0.84 : 0.68;
        tintBgBlendRatio = 0.04;
        borderTintSaturationScale = 0.90;
        borderTintValueScale = darkVariant ? 0.88 : 0.68;
        borderBlendRatio = 0.08;
        break;
    case an::ui::ThemeContrastProfile::Oled:
        // Light-mode values are unreachable (the OLED branch above
        // returns early with Balanced), but kept for switch completeness.
        tintSaturationScale = 1.00;
        tintValueScale = darkVariant ? 0.90 : 0.60;
        tintBgBlendRatio = 0.02;
        borderTintSaturationScale = 1.00;
        borderTintValueScale = darkVariant ? 0.95 : 0.60;
        borderBlendRatio = 0.05;
        break;
    case an::ui::ThemeContrastProfile::Balanced:
        tintSaturationScale = 0.82;
        tintValueScale = 0.76;
        tintBgBlendRatio = 0.08;
        borderTintSaturationScale = 0.80;
        borderTintValueScale = 0.78;
        borderBlendRatio = 0.12;
        break;
    case an::ui::ThemeContrastProfile::Soft:
        tintSaturationScale = 0.74;
        tintValueScale = darkVariant ? 0.68 : 0.84;
        tintBgBlendRatio = 0.12;
        borderTintSaturationScale = 0.72;
        borderTintValueScale = darkVariant ? 0.70 : 0.88;
        borderBlendRatio = 0.18;
        break;
    }

    if (c.cardSurfaceTint.isValid())
    {
        c.cardSurfaceTint = adjustSaturationValue(c.cardSurfaceTint, tintSaturationScale, tintValueScale);
        c.cardSurfaceTint = blendRgb(c.cardSurfaceTint, c.bgCard, tintBgBlendRatio);
    }

    if (c.cardSurfaceBorderTint.isValid())
    {
        c.cardSurfaceBorderTint =
            adjustSaturationValue(c.cardSurfaceBorderTint, borderTintSaturationScale, borderTintValueScale);
        c.cardSurfaceBorderTint = blendRgb(c.cardSurfaceBorderTint, c.borderPrimary, borderBlendRatio);
    }

    refreshLayerAndMaterialMappings(tokenSet);
    return tokenSet;
}

an::ui::MaterialTokens resolvedMaterials(const an::ui::DesignTokens& tokenSet, const bool darkVariant,
                                         const bool useNativeBackdrop)
{
    if (!useNativeBackdrop)
    {
        return tokenSet.materials;
    }

    an::ui::MaterialTokens materials = tokenSet.materials;
    materials.solidSurface = tokenSet.layers.surface1;
    materials.micaSurface = withAlpha(tokenSet.layers.surface0, darkVariant ? 232 : 245);
    materials.acrylicSurface = withAlpha(tokenSet.colors.bgCard, darkVariant ? 204 : 226);
    return materials;
}

an::ui::DesignTokens resolvedTokenSet(const an::ui::DesignTokens& base, const bool darkVariant,
                                      const bool useNativeBackdrop, const an::ui::ThemeContrastProfile contrastProfile,
                                      const an::ui::ThemeAccentFamily accentFamily,
                                      const an::ui::ThemeSurfaceTintFamily surfaceTintFamily)
{
    const an::ui::DesignTokens accented = applyThemeAccentFamily(base, darkVariant, accentFamily);
    const an::ui::DesignTokens tinted = applyThemeSurfaceTintFamily(accented, darkVariant, surfaceTintFamily);
    an::ui::DesignTokens resolved = applyThemeContrastProfile(tinted, darkVariant, contrastProfile);

    const QColor white(255, 255, 255);
    const QColor black(0, 0, 0);
    const double contrastWhite = contrastRatio(white, resolved.colors.accentPrimary);
    const double contrastBlack = contrastRatio(black, resolved.colors.accentPrimary);
    resolved.colors.textOnAccent = (contrastWhite >= 4.5 || contrastWhite > contrastBlack) ? white : black;

    resolved.materials = resolvedMaterials(resolved, darkVariant, useNativeBackdrop);
    return resolved;
}

void ensureThemeInitialized();

struct ResolvedTokenSnapshot
{
    an::ui::DesignTokens darkTokens;
    an::ui::DesignTokens lightTokens;
};

std::atomic_bool g_isDarkTheme{true};
std::atomic_bool g_isThemeInitialized{false};
std::atomic_int g_themePreference{static_cast<int>(an::ui::ThemePreference::System)};
std::atomic_int g_materialBackdropMode{static_cast<int>(an::ui::MaterialBackdropMode::Auto)};
std::atomic_int g_themeContrastProfile{static_cast<int>(an::ui::ThemeContrastProfile::Balanced)};
std::atomic_int g_themeAccentFamily{static_cast<int>(an::ui::ThemeAccentFamily::Blue)};
std::atomic_int g_themeSurfaceTintFamily{static_cast<int>(an::ui::ThemeSurfaceTintFamily::Neutral)};
std::atomic_int g_themeTintStrength{static_cast<int>(an::ui::ThemeTintStrength::Balanced)};
std::atomic_int g_themeDensity{static_cast<int>(an::ui::ThemeDensity::Compact)};
std::atomic_int g_styleProfile{static_cast<int>(an::ui::StyleProfile::Classic)};
std::atomic_bool g_nightMode{false};
std::atomic_bool g_nativeBackdropSupported{detectNativeBackdropSupport()};
std::atomic<const ResolvedTokenSnapshot*> g_activeResolvedTokens{nullptr};
std::vector<std::unique_ptr<ResolvedTokenSnapshot>> g_resolvedTokenSnapshotHistory;
std::mutex g_resolvedTokenSnapshotHistoryMutex;

void publishResolvedTokenSnapshot(an::ui::DesignTokens darkTokens, an::ui::DesignTokens lightTokens)
{
    auto snapshot = std::make_unique<ResolvedTokenSnapshot>();
    snapshot->darkTokens = std::move(darkTokens);
    snapshot->lightTokens = std::move(lightTokens);
    const ResolvedTokenSnapshot* snapshotPtr = snapshot.get();

    {
        const std::lock_guard<std::mutex> lock(g_resolvedTokenSnapshotHistoryMutex);
        g_resolvedTokenSnapshotHistory.emplace_back(std::move(snapshot));
    }

    g_activeResolvedTokens.store(snapshotPtr, std::memory_order_release);
}

const ResolvedTokenSnapshot& activeResolvedTokenSnapshot()
{
    ensureThemeInitialized();

    const ResolvedTokenSnapshot* snapshot = g_activeResolvedTokens.load(std::memory_order_acquire);
    if (!snapshot)
    {
        an::ui::refreshTokens();
        snapshot = g_activeResolvedTokens.load(std::memory_order_acquire);
    }

    if (!snapshot)
    {
        static const ResolvedTokenSnapshot fallback{an::ui::darkTokens(), an::ui::lightTokens()};
        return fallback;
    }

    return *snapshot;
}

void ensureThemeInitialized()
{
    if (!g_isThemeInitialized.load(std::memory_order_acquire))
    {
        an::ui::refreshTokens();
    }
}

/// The choices in force, for one theme.
an::ui::ThemeChoices activeChoices(const an::ui::ThemeVariant theme, const bool nativeBackdrop)
{
    return an::ui::ThemeChoices{
        .theme = theme,
        .contrast = static_cast<an::ui::ThemeContrastProfile>(g_themeContrastProfile.load()),
        .accent = static_cast<an::ui::ThemeAccentFamily>(g_themeAccentFamily.load()),
        .tint = static_cast<an::ui::ThemeSurfaceTintFamily>(g_themeSurfaceTintFamily.load()),
        .tintStrength = static_cast<an::ui::ThemeTintStrength>(g_themeTintStrength.load()),
        .profile = static_cast<an::ui::StyleProfile>(g_styleProfile.load()),
        .density = static_cast<an::ui::ThemeDensity>(g_themeDensity.load()),
        .night = g_nightMode.load(),
        .nativeBackdrop = nativeBackdrop,
    };
}

/// Whether Night is in force: set, under the Observatory profile, the only one that has it.
bool nightInForce()
{
    return g_nightMode.load() &&
           static_cast<an::ui::StyleProfile>(g_styleProfile.load()) == an::ui::StyleProfile::Observatory;
}

/// The presets and their parts, in the order the Interface page shows them.
constexpr std::array<std::pair<an::ui::ThemePreset, an::ui::ThemePresetParts>, 6u> kPresets{{
    {an::ui::ThemePreset::SlateBlue,
     {an::ui::ThemeAccentFamily::Blue, an::ui::ThemeSurfaceTintFamily::Neutral, an::ui::ThemeTintStrength::Balanced}},
    {an::ui::ThemePreset::Observatory,
     {an::ui::ThemeAccentFamily::Amber, an::ui::ThemeSurfaceTintFamily::Ocean, an::ui::ThemeTintStrength::Strong}},
    {an::ui::ThemePreset::DeepSea,
     {an::ui::ThemeAccentFamily::Teal, an::ui::ThemeSurfaceTintFamily::Ocean, an::ui::ThemeTintStrength::Balanced}},
    {an::ui::ThemePreset::Graphite,
     {an::ui::ThemeAccentFamily::Amber, an::ui::ThemeSurfaceTintFamily::Neutral, an::ui::ThemeTintStrength::Balanced}},
    {an::ui::ThemePreset::Nebula,
     {an::ui::ThemeAccentFamily::Rose, an::ui::ThemeSurfaceTintFamily::Amethyst, an::ui::ThemeTintStrength::Balanced}},
    {an::ui::ThemePreset::Aurora,
     {an::ui::ThemeAccentFamily::Teal, an::ui::ThemeSurfaceTintFamily::Mint, an::ui::ThemeTintStrength::Subtle}},
}};

/// Whether a widget exists in the process: a profile is fixed once one does.
bool anyWidgetExists()
{
    return qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr &&
           !QApplication::allWidgets().isEmpty();
}

} // anonymous namespace

namespace an::ui
{

const DesignTokens& darkTokens()
{
    static const DesignTokens tokenSet = createDarkTokens();
    return tokenSet;
}

const DesignTokens& lightTokens()
{
    static const DesignTokens tokenSet = createLightTokens();
    return tokenSet;
}

DesignTokens resolveDesignTokens(const ThemeChoices& choices)
{
    if (choices.profile == StyleProfile::Observatory)
    {
        // Night resolves the dark theme whatever the preference, with the tint off whatever the tint and its
        // strength: a hue cannot be seen in red, and a tinted surface would leave red text under its contrast.
        ThemeChoices resolvedChoices = choices;
        if (choices.night)
        {
            resolvedChoices.theme = ThemeVariant::Dark;
            resolvedChoices.tint = ThemeSurfaceTintFamily::Neutral;
            resolvedChoices.tintStrength = ThemeTintStrength::Off;
        }
        const bool observatoryDark = resolvedChoices.theme == ThemeVariant::Dark;
        DesignTokens resolved = observatoryDark ? darkTokens() : lightTokens();
        resolveObservatoryColors(resolved, resolvedChoices);
        resolved.materials = resolvedMaterials(resolved, observatoryDark, choices.nativeBackdrop);
        resolveObservatorySizes(resolved, choices.density);
        if (choices.night)
        {
            mapToNight(resolved);
        }
        return resolved;
    }
    // The Classic profile has no Night.
    const bool dark = choices.theme == ThemeVariant::Dark;
    const DesignTokens& base = dark ? darkTokens() : lightTokens();
    DesignTokens resolved =
        resolvedTokenSet(base, dark, choices.nativeBackdrop, choices.contrast, choices.accent, choices.tint);
    const QColor darkWindow = dark ? resolved.layers.surface0
                                   : resolvedTokenSet(darkTokens(), true, choices.nativeBackdrop, choices.contrast,
                                                      choices.accent, choices.tint)
                                         .layers.surface0;
    applyClassicColorRoles(resolved, dark, darkWindow);
    return resolved;
}

const DesignTokens& resolvedDarkTokens()
{
    return activeResolvedTokenSnapshot().darkTokens;
}

const DesignTokens& resolvedLightTokens()
{
    return activeResolvedTokenSnapshot().lightTokens;
}

const DesignTokens& tokens()
{
    const auto& snapshot = activeResolvedTokenSnapshot();
    return g_isDarkTheme.load(std::memory_order_acquire) ? snapshot.darkTokens : snapshot.lightTokens;
}

QColor compositeOver(const QColor& foreground, const QColor& background)
{
    return ::compositeOver(foreground, background);
}

double contrastRatio(const QColor& a, const QColor& b)
{
    return ::contrastRatio(a, b);
}

QColor bestContrastTextColor(const QColor& background, const QColor& preferred, const QColor& fallbackA,
                             const QColor& fallbackB, const double minimumRatio)
{
    const double preferredRatio = an::ui::contrastRatio(background, preferred);
    if (preferredRatio >= minimumRatio)
    {
        return preferred;
    }

    const double fallbackARatio = an::ui::contrastRatio(background, fallbackA);
    const double fallbackBRatio = an::ui::contrastRatio(background, fallbackB);

    if (fallbackARatio >= fallbackBRatio)
    {
        return fallbackARatio >= preferredRatio ? fallbackA : preferred;
    }

    return fallbackBRatio >= preferredRatio ? fallbackB : preferred;
}

QColor semanticColor(const ThemeVariant variant, const SemanticColorRole role)
{
    ensureThemeInitialized();
    return semanticColorFromTokens(variant, role);
}

QColor semanticColor(const bool darkTheme, const SemanticColorRole role)
{
    return semanticColor(darkTheme ? ThemeVariant::Dark : ThemeVariant::Light, role);
}

QColor semanticColor(const SemanticColorRole role)
{
    ensureThemeInitialized();
    return semanticColor(g_isDarkTheme.load() ? ThemeVariant::Dark : ThemeVariant::Light, role);
}

void refreshTokens()
{
    const auto preference = static_cast<ThemePreference>(g_themePreference.load());

    switch (preference)
    {
    case ThemePreference::Dark:
        g_isDarkTheme.store(true);
        break;
    case ThemePreference::Light:
        g_isDarkTheme.store(false);
        break;
    case ThemePreference::System:
    default:
        g_isDarkTheme.store(detectDarkThemeFromApplication());
        break;
    }
    // Night is dark whatever the preference: both sets resolve its one dark set, and every widget that asks which
    // theme it paints is told the dark one.
    if (nightInForce())
    {
        g_isDarkTheme.store(true);
    }

    const bool nativeSupported = detectNativeBackdropSupport();
    g_nativeBackdropSupported.store(nativeSupported);

    const auto backdropMode = static_cast<MaterialBackdropMode>(g_materialBackdropMode.load());
    const bool useNativeBackdrop = shouldUseNativeBackdropMaterials(backdropMode, nativeSupported);
    const ThemeChoices darkChoices = activeChoices(ThemeVariant::Dark, useNativeBackdrop);
    const ThemeChoices lightChoices = activeChoices(ThemeVariant::Light, useNativeBackdrop);

    DesignTokens resolvedDarkTokenSet = resolveDesignTokens(darkChoices);
    DesignTokens resolvedLightTokenSet = resolveDesignTokens(lightChoices);

    validateTokenContrastRatios(resolvedDarkTokenSet, darkChoices);
    validateTokenContrastRatios(resolvedLightTokenSet, lightChoices);

    if (tokenDebugDumpEnabled() && an::platform::isLoggingInitialized())
    {
        LOG_INFO("{}", buildResolvedTokenDumpReport(resolvedDarkTokenSet, resolvedLightTokenSet,
                                                    g_isDarkTheme.load(std::memory_order_acquire), darkChoices));
    }

    // A published snapshot is kept for the life of the process, so that a reference tokens() handed out stays valid.
    // Every component refreshes the tokens when it refreshes its theme, so a refresh that resolves the sets already
    // active publishes nothing: the kept snapshots are one per distinct theme a session shows, not one per refresh.
    const ResolvedTokenSnapshot* active = g_activeResolvedTokens.load(std::memory_order_acquire);
    const bool unchanged =
        active != nullptr && active->darkTokens == resolvedDarkTokenSet && active->lightTokens == resolvedLightTokenSet;
    if (!unchanged)
    {
        publishResolvedTokenSnapshot(std::move(resolvedDarkTokenSet), std::move(resolvedLightTokenSet));
    }

    g_isThemeInitialized.store(true, std::memory_order_release);
}

std::string dumpResolvedTokensReport()
{
    const auto& snapshot = activeResolvedTokenSnapshot();
    return buildResolvedTokenDumpReport(snapshot.darkTokens, snapshot.lightTokens,
                                        g_isDarkTheme.load(std::memory_order_acquire),
                                        activeChoices(ThemeVariant::Dark, usingNativeBackdropMaterials()));
}

std::string dumpResolvedTokenDiffReport()
{
    const auto& snapshot = activeResolvedTokenSnapshot();
    return buildResolvedTokenDiffReport(snapshot.darkTokens, snapshot.lightTokens);
}

void logResolvedTokensReport()
{
    if (!an::platform::isLoggingInitialized())
    {
        return;
    }

    LOG_INFO("{}", dumpResolvedTokensReport());
}

void setThemePreference(const ThemePreference preference)
{
    g_themePreference.store(static_cast<int>(preference));
    refreshTokens();
}

ThemePreference themePreference()
{
    return static_cast<ThemePreference>(g_themePreference.load());
}

void setMaterialBackdropMode(const MaterialBackdropMode mode)
{
    g_materialBackdropMode.store(static_cast<int>(mode));
    refreshTokens();
}

MaterialBackdropMode materialBackdropMode()
{
    return static_cast<MaterialBackdropMode>(g_materialBackdropMode.load());
}

void setThemeContrastProfile(const ThemeContrastProfile profile)
{
    g_themeContrastProfile.store(static_cast<int>(profile));
    refreshTokens();
}

ThemeContrastProfile themeContrastProfile()
{
    return static_cast<ThemeContrastProfile>(g_themeContrastProfile.load());
}

void setThemeAccentFamily(const ThemeAccentFamily family)
{
    g_themeAccentFamily.store(static_cast<int>(family));
    refreshTokens();
}

ThemeAccentFamily themeAccentFamily()
{
    return static_cast<ThemeAccentFamily>(g_themeAccentFamily.load());
}

void setThemeSurfaceTintFamily(const ThemeSurfaceTintFamily family)
{
    g_themeSurfaceTintFamily.store(static_cast<int>(family));
    refreshTokens();
}

ThemeSurfaceTintFamily themeSurfaceTintFamily()
{
    return static_cast<ThemeSurfaceTintFamily>(g_themeSurfaceTintFamily.load());
}

void setThemeTintStrength(const ThemeTintStrength strength)
{
    g_themeTintStrength.store(static_cast<int>(strength));
    refreshTokens();
}

ThemeTintStrength themeTintStrength()
{
    return static_cast<ThemeTintStrength>(g_themeTintStrength.load());
}

void setThemeDensity(const ThemeDensity density)
{
    g_themeDensity.store(static_cast<int>(density));
    refreshTokens();
}

ThemeDensity themeDensity()
{
    return static_cast<ThemeDensity>(g_themeDensity.load());
}

std::optional<ThemePresetParts> themePresetParts(const ThemePreset preset)
{
    for (const auto& [named, parts] : kPresets)
    {
        if (named == preset)
        {
            return parts;
        }
    }
    return std::nullopt;
}

ThemePreset themePresetOf(const ThemeAccentFamily accent, const ThemeSurfaceTintFamily tint,
                          const ThemeTintStrength strength)
{
    const ThemePresetParts wanted{.accent = accent, .tint = tint, .strength = strength};
    for (const auto& [preset, parts] : kPresets)
    {
        if (parts == wanted)
        {
            return preset;
        }
    }
    return ThemePreset::Custom;
}

void applyThemePreset(const ThemePreset preset)
{
    const std::optional<ThemePresetParts> parts = themePresetParts(preset);
    if (!parts.has_value())
    {
        return;
    }
    g_themeAccentFamily.store(static_cast<int>(parts->accent));
    g_themeSurfaceTintFamily.store(static_cast<int>(parts->tint));
    g_themeTintStrength.store(static_cast<int>(parts->strength));
    refreshTokens();
}

ThemePreset themePreset()
{
    return themePresetOf(themeAccentFamily(), themeSurfaceTintFamily(), themeTintStrength());
}

void setNightMode(const bool night)
{
    g_nightMode.store(night);
    if (!night)
    {
        clearNightImages();
    }
    refreshTokens();
}

bool nightModeSet()
{
    return g_nightMode.load();
}

bool isNightMode()
{
    return nightInForce();
}

bool setStyleProfile(const StyleProfile profile)
{
    if (profile == styleProfile())
    {
        return true;
    }
    if (anyWidgetExists())
    {
        LOG_WARNING("The {} style profile is refused: widgets exist under the {} profile, which stays in force",
                    styleProfileLabel(profile), styleProfileLabel(styleProfile()));
        return false;
    }
    g_styleProfile.store(static_cast<int>(profile));
    refreshTokens();
    return true;
}

StyleProfile styleProfile()
{
    return static_cast<StyleProfile>(g_styleProfile.load());
}

namespace testing
{
void forceStyleProfile(const StyleProfile profile)
{
    g_styleProfile.store(static_cast<int>(profile));
    refreshTokens();
}
} // namespace testing

bool nativeBackdropSupported()
{
    ensureThemeInitialized();
    return g_nativeBackdropSupported.load();
}

bool usingNativeBackdropMaterials()
{
    ensureThemeInitialized();
    return shouldUseNativeBackdropMaterials(static_cast<MaterialBackdropMode>(g_materialBackdropMode.load()),
                                            g_nativeBackdropSupported.load());
}

bool isDarkTheme()
{
    ensureThemeInitialized();
    return g_isDarkTheme.load();
}

} // namespace an::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <set>

#include <QColor>
#include <QIcon>
#include <QString>
#include <QWidget>

#include "theme/AnDesignTokens.h"

class ImageViewControlManager;
class MainDashboard;
class QToolButton;
class TopLevelProcessBaseControl;

namespace KDDockWidgets::QtWidgets
{
class MainWindow;
}

namespace an::ui
{

inline Qt::ColorScheme resolveThemeColorScheme(const QString& themeId)
{
    const QString normalizedThemeId = themeId.trimmed().toLower();

    if (normalizedThemeId == QStringLiteral("dark"))
    {
        return Qt::ColorScheme::Dark;
    }

    if (normalizedThemeId == QStringLiteral("light"))
    {
        return Qt::ColorScheme::Light;
    }

    return Qt::ColorScheme::Unknown;
}

inline bool resolveDarkModeForTheme(const QString& themeId, const bool currentDarkMode)
{
    const QString normalizedThemeId = themeId.trimmed().toLower();

    if (normalizedThemeId == QStringLiteral("dark"))
    {
        return true;
    }

    if (normalizedThemeId == QStringLiteral("light"))
    {
        return false;
    }

    return currentDarkMode;
}

inline MaterialBackdropMode resolveMaterialBackdropMode(const QString& modeId)
{
    const QString normalizedModeId = modeId.trimmed().toLower();

    if (normalizedModeId == QStringLiteral("native"))
    {
        return MaterialBackdropMode::PreferNative;
    }

    if (normalizedModeId == QStringLiteral("fallback"))
    {
        return MaterialBackdropMode::ForceFallback;
    }

    return MaterialBackdropMode::Auto;
}

inline QString materialBackdropModeId(const MaterialBackdropMode mode)
{
    switch (mode)
    {
    case MaterialBackdropMode::PreferNative:
        return QStringLiteral("native");
    case MaterialBackdropMode::ForceFallback:
        return QStringLiteral("fallback");
    case MaterialBackdropMode::Auto:
    default:
        return QStringLiteral("auto");
    }
}

inline ThemeContrastProfile resolveThemeContrastProfile(const QString& profileId)
{
    const QString normalizedProfileId = profileId.trimmed().toLower();

    if (normalizedProfileId == QStringLiteral("high"))
    {
        return ThemeContrastProfile::High;
    }

    if (normalizedProfileId == QStringLiteral("soft"))
    {
        return ThemeContrastProfile::Soft;
    }

    if (normalizedProfileId == QStringLiteral("oled"))
    {
        return ThemeContrastProfile::Oled;
    }

    return ThemeContrastProfile::Balanced;
}

inline QString themeContrastProfileId(const ThemeContrastProfile profile)
{
    switch (profile)
    {
    case ThemeContrastProfile::High:
        return QStringLiteral("high");
    case ThemeContrastProfile::Soft:
        return QStringLiteral("soft");
    case ThemeContrastProfile::Oled:
        return QStringLiteral("oled");
    case ThemeContrastProfile::Balanced:
    default:
        return QStringLiteral("balanced");
    }
}

inline ThemeAccentFamily resolveThemeAccentFamily(const QString& familyId)
{
    const QString normalizedFamilyId = familyId.trimmed().toLower();

    if (normalizedFamilyId == QStringLiteral("teal"))
    {
        return ThemeAccentFamily::Teal;
    }

    if (normalizedFamilyId == QStringLiteral("amber"))
    {
        return ThemeAccentFamily::Amber;
    }

    if (normalizedFamilyId == QStringLiteral("amethyst"))
    {
        return ThemeAccentFamily::Amethyst;
    }

    if (normalizedFamilyId == QStringLiteral("rose"))
    {
        return ThemeAccentFamily::Rose;
    }

    return ThemeAccentFamily::Blue;
}

inline QString themeAccentFamilyId(const ThemeAccentFamily family)
{
    switch (family)
    {
    case ThemeAccentFamily::Teal:
        return QStringLiteral("teal");
    case ThemeAccentFamily::Amber:
        return QStringLiteral("amber");
    case ThemeAccentFamily::Amethyst:
        return QStringLiteral("amethyst");
    case ThemeAccentFamily::Rose:
        return QStringLiteral("rose");
    case ThemeAccentFamily::Blue:
    default:
        return QStringLiteral("blue");
    }
}

inline ThemeSurfaceTintFamily resolveThemeSurfaceTintFamily(const QString& familyId)
{
    const QString normalizedFamilyId = familyId.trimmed().toLower();

    if (normalizedFamilyId == QStringLiteral("ocean"))
    {
        return ThemeSurfaceTintFamily::Ocean;
    }

    if (normalizedFamilyId == QStringLiteral("mint"))
    {
        return ThemeSurfaceTintFamily::Mint;
    }

    if (normalizedFamilyId == QStringLiteral("amethyst"))
    {
        return ThemeSurfaceTintFamily::Amethyst;
    }

    if (normalizedFamilyId == QStringLiteral("rose"))
    {
        return ThemeSurfaceTintFamily::Rose;
    }

    if (normalizedFamilyId == QStringLiteral("amber"))
    {
        return ThemeSurfaceTintFamily::Amber;
    }

    return ThemeSurfaceTintFamily::Neutral;
}

inline QString themeSurfaceTintFamilyId(const ThemeSurfaceTintFamily family)
{
    switch (family)
    {
    case ThemeSurfaceTintFamily::Ocean:
        return QStringLiteral("ocean");
    case ThemeSurfaceTintFamily::Mint:
        return QStringLiteral("mint");
    case ThemeSurfaceTintFamily::Amethyst:
        return QStringLiteral("amethyst");
    case ThemeSurfaceTintFamily::Rose:
        return QStringLiteral("rose");
    case ThemeSurfaceTintFamily::Amber:
        return QStringLiteral("amber");
    case ThemeSurfaceTintFamily::Neutral:
    default:
        return QStringLiteral("neutral");
    }
}

/// The tint strength a setting names: `off`, `subtle`, `balanced` or `strong`, in any case; anything else is Balanced.
inline ThemeTintStrength resolveThemeTintStrength(const QString& strengthId)
{
    const QString normalizedStrengthId = strengthId.trimmed().toLower();

    if (normalizedStrengthId == QStringLiteral("off"))
    {
        return ThemeTintStrength::Off;
    }

    if (normalizedStrengthId == QStringLiteral("subtle"))
    {
        return ThemeTintStrength::Subtle;
    }

    if (normalizedStrengthId == QStringLiteral("strong"))
    {
        return ThemeTintStrength::Strong;
    }

    return ThemeTintStrength::Balanced;
}

/// The setting's value for a tint strength.
inline QString themeTintStrengthId(const ThemeTintStrength strength)
{
    switch (strength)
    {
    case ThemeTintStrength::Off:
        return QStringLiteral("off");
    case ThemeTintStrength::Subtle:
        return QStringLiteral("subtle");
    case ThemeTintStrength::Strong:
        return QStringLiteral("strong");
    case ThemeTintStrength::Balanced:
    default:
        return QStringLiteral("balanced");
    }
}

inline std::pair<QColor, QColor> iconPaletteForTheme(const bool darkMode)
{
    const auto& colors = darkMode ? resolvedDarkTokens().colors : resolvedLightTokens().colors;
    return {colors.textPrimary, colors.textTertiary};
}

struct ThemeIconRefreshRequest
{
    QColor normalIconColor;
    QColor disabledIconColor;
    QColor infoIconColor;
    QColor warningIconColor;
    QColor errorIconColor;
    QColor questionIconColor;
    QColor successIconColor;
};

inline ThemeIconRefreshRequest buildThemeIconRefreshRequest(const bool darkMode)
{
    const auto [normalIconColor, disabledIconColor] = iconPaletteForTheme(darkMode);
    const auto& colorTokens = an::ui::tokens().colors;

    return ThemeIconRefreshRequest{
        normalIconColor,         disabledIconColor,         colorTokens.statusInfo,    colorTokens.statusWarning,
        colorTokens.statusError, colorTokens.accentPrimary, colorTokens.statusSuccess,
    };
}

class AFTERNIGHTCONTROLS_API ThemeManager
{
public:
    struct Dependencies
    {
        bool* darkMode = nullptr;
        std::function<void(const ThemeIconRefreshRequest&)> reloadIcons;
        std::function<void()> setActionIcons;

        // Cascade dependencies
        MainDashboard** mainDashboard = nullptr;
        QToolButton** toolbarLockLayout = nullptr;
        std::function<bool()> isLayoutLocked;
        ImageViewControlManager* imageViewControlManager = nullptr;
        KDDockWidgets::QtWidgets::MainWindow** dockMainWindow = nullptr;
        /// Visits each open process window, under the lock of whatever holds them; the visit may not open or close
        /// one.
        std::function<void(const std::function<void(TopLevelProcessBaseControl*)>&)> forEachOpenProcess;
        QWidget* mainWindow = nullptr;
        std::function<QIcon(const std::string&)> iconProvider;
    };

    explicit ThemeManager(const Dependencies& deps);

    void refreshTheme() const;
    void loadIcons() const;
    void applyTheme(const QString& themeId) const;

private:
    void refreshCascade() const;

    Dependencies m_deps;
};

} // namespace an::ui

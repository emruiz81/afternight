// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewThemeColors.h"

#include "app/UiStyleTokens.h"
#include "theme/AnDesignTokens.h"

#include <algorithm>

namespace
{

QColor blend(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  base.alpha());
}

} // anonymous namespace

namespace an::ui
{

QColor imageViewScrollAreaWindowColor(const bool darkMode)
{
    const auto themeVariant = darkMode ? styling::ThemeVariant::Dark : styling::ThemeVariant::Light;
    const QColor surfaceWindow = styling::semanticColor(themeVariant, styling::SemanticColorRole::SurfaceWindow);
    const QColor surfaceBorder = styling::semanticColor(themeVariant, styling::SemanticColorRole::SurfaceBorder);
    return blend(surfaceWindow, surfaceBorder, darkMode ? 0.22 : 0.52);
}

QColor imageViewCardSurfaceTintColor(const bool darkMode)
{
    const auto& tokenSet = darkMode ? resolvedDarkTokens() : resolvedLightTokens();

    QColor surface = tokenSet.materials.acrylicSurface;
    if (tokenSet.colors.cardSurfaceTint.isValid())
    {
        surface = blend(surface, tokenSet.colors.cardSurfaceTint, darkMode ? 0.56 : 0.44);
        if (darkMode)
        {
            surface = blend(surface, QColor(0, 0, 0), 0.22);
        }
    }

    if (surface.alpha() < 255)
    {
        const QColor base = tokenSet.materials.micaSurface;
        const double a = static_cast<double>(surface.alpha()) / 255.0;
        const auto composite = [a](const int fg, const int bg) {
            return static_cast<int>(std::round(fg * a + bg * (1.0 - a)));
        };

        surface = QColor(composite(surface.red(), base.red()), composite(surface.green(), base.green()),
                         composite(surface.blue(), base.blue()), 255);
    }

    return surface;
}

QColor imageViewOverlayAutostretchColor(const bool darkMode)
{
    Q_UNUSED(darkMode);
    const auto themeVariant = styling::ThemeVariant::Dark;
    const QColor textPrimary = styling::semanticColor(themeVariant, styling::SemanticColorRole::TextPrimary);
    const QColor statusWarning = styling::semanticColor(themeVariant, styling::SemanticColorRole::StatusWarning);
    return blend(textPrimary, statusWarning, 0.58);
}

QColor imageViewOverlayOriginalColor(const bool darkMode)
{
    Q_UNUSED(darkMode);
    const auto themeVariant = styling::ThemeVariant::Dark;
    const QColor textPrimary = styling::semanticColor(themeVariant, styling::SemanticColorRole::TextPrimary);
    const QColor statusError = styling::semanticColor(themeVariant, styling::SemanticColorRole::StatusError);
    return blend(textPrimary, statusError, 0.62);
}

QColor imageViewOverlayBackdropColor(const bool darkMode)
{
    Q_UNUSED(darkMode);
    const auto themeVariant = styling::ThemeVariant::Dark;
    QColor backdrop = styling::semanticColor(themeVariant, styling::SemanticColorRole::SurfaceWindow);
    backdrop.setAlpha(170);
    return backdrop;
}

} // namespace an::ui

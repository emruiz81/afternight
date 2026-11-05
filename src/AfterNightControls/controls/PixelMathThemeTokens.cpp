// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PixelMathThemeTokens.h"

#include "app/UiStyleTokens.h"

#include <algorithm>

namespace
{

QColor blend(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  lerp(base.alpha(), tint.alpha()));
}

} // anonymous namespace

namespace an::ui
{

PixelMathSyntaxPalette pixelMathSyntaxPalette(const bool darkMode)
{
    const auto themeVariant = darkMode ? styling::ThemeVariant::Dark : styling::ThemeVariant::Light;
    using styling::SemanticColorRole;
    const auto semantic = [themeVariant](const SemanticColorRole role) {
        return styling::semanticColor(themeVariant, role);
    };

    PixelMathSyntaxPalette palette;
    palette.editorBackground = semantic(SemanticColorRole::CodeBackground);
    palette.functionColor = semantic(SemanticColorRole::AccentPrimary);
    palette.specialFunctionColor = semantic(SemanticColorRole::AccentHover);
    palette.imageColor = semantic(SemanticColorRole::StatusSuccess);
    palette.symbolColor = semantic(SemanticColorRole::StatusActive);
    palette.commentColor =
        blend(semantic(SemanticColorRole::StatusSuccess), semantic(SemanticColorRole::TextSecondary), 0.35);
    palette.punctuationColor = semantic(SemanticColorRole::TextSecondary);
    palette.bracketColor = semantic(SemanticColorRole::TextSecondary);
    palette.operatorColor = semantic(SemanticColorRole::TextPrimary);
    palette.digitColor = blend(semantic(SemanticColorRole::StatusActive), semantic(SemanticColorRole::TextPrimary),
                               darkMode ? 0.36 : 0.58);
    return palette;
}

} // namespace an::ui

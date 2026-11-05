// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "UiStyleTokens.h"

namespace an::ui::styling
{

bool isDarkPalette(const QPalette& palette)
{
    return palette.color(QPalette::Window).value() < 128;
}

ThemeVariant detectThemeVariant(const QPalette& palette)
{
    return isDarkPalette(palette) ? ThemeVariant::Dark : ThemeVariant::Light;
}

QColor semanticColor(const ThemeVariant themeVariant, const SemanticColorRole role)
{
    return an::ui::semanticColor(themeVariant, role);
}

QColor semanticColor(const QPalette& palette, const SemanticColorRole role)
{
    return an::ui::styling::semanticColor(detectThemeVariant(palette), role);
}

} // namespace an::ui::styling

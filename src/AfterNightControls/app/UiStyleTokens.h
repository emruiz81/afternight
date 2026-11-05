// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "theme/AnDesignTokens.h"

#include <QColor>
#include <QPalette>

namespace an::ui::styling
{

using ThemeVariant = an::ui::ThemeVariant;
using SemanticColorRole = an::ui::SemanticColorRole;

AFTERNIGHTCONTROLS_API bool isDarkPalette(const QPalette& palette);
AFTERNIGHTCONTROLS_API ThemeVariant detectThemeVariant(const QPalette& palette);
AFTERNIGHTCONTROLS_API QColor semanticColor(ThemeVariant themeVariant, SemanticColorRole role);
AFTERNIGHTCONTROLS_API QColor semanticColor(const QPalette& palette, SemanticColorRole role);

} // namespace an::ui::styling

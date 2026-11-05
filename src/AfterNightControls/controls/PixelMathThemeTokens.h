// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>

namespace an::ui
{

struct PixelMathSyntaxPalette
{
    QColor editorBackground;
    QColor functionColor;
    QColor specialFunctionColor;
    QColor imageColor;
    QColor symbolColor;
    QColor commentColor;
    QColor punctuationColor;
    QColor bracketColor;
    QColor operatorColor;
    QColor digitColor;
};

[[nodiscard]] AFTERNIGHTCONTROLS_API PixelMathSyntaxPalette pixelMathSyntaxPalette(bool darkMode);

} // namespace an::ui

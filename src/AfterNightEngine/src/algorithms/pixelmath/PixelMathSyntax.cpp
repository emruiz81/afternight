// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the written spellings the syntax vocabulary reports.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>

namespace an
{

const char* pixelMathMetaSymbolName(PixelMathMetaSymbol symbol) noexcept
{
    switch (symbol)
    {
    case PixelMathMetaSymbol::Target:
        return "$T";
    case PixelMathMetaSymbol::Active:
        return "$A";
    case PixelMathMetaSymbol::Mask:
        return "$M";
    }
    return "unknown";
}

} // namespace an

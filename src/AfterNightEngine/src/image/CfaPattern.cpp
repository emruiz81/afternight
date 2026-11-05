// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/CfaPattern.h>

// The names of the described mosaics and of the filters they are made of. The block size, the phase
// rules and the table validation are constant expressions in the header; a name is read by a person
// and by the text form, so it costs a call and lives here.
//
// The switch has no `default` label, so a pattern added to the enumeration without a name is a
// compile failure rather than a blank in a log.

namespace an
{

const char* cfaPatternName(CfaPattern pattern) noexcept
{
    switch (pattern)
    {
    case CfaPattern::None:
        return "None";
    case CfaPattern::Rggb:
        return "Rggb";
    case CfaPattern::Bggr:
        return "Bggr";
    case CfaPattern::Grbg:
        return "Grbg";
    case CfaPattern::Gbrg:
        return "Gbrg";
    case CfaPattern::XTrans:
        return "XTrans";
    }
    return "UnknownCfaPattern";
}

const char* cfaColorName(CfaColor color) noexcept
{
    switch (color)
    {
    case CfaColor::Red:
        return "Red";
    case CfaColor::Green:
        return "Green";
    case CfaColor::Blue:
        return "Blue";
    }
    return "UnknownCfaColor";
}

} // namespace an

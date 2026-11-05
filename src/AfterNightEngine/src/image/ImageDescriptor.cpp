// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ImageDescriptor.h>

// The name of the coordinate origin. Everything the descriptor decides is a constant expression in
// the header, so this is the whole of what the composite costs at run time: the one string a log
// line or the text form needs.
//
// The switch has no `default` label, so an origin added to the enumeration without a name is a
// compile failure rather than a blank in a log.

namespace an
{

const char* coordinateOriginName(CoordinateOrigin origin) noexcept
{
    switch (origin)
    {
    case CoordinateOrigin::TopLeft:
        return "TopLeft";
    case CoordinateOrigin::BottomLeft:
        return "BottomLeft";
    }
    return "UnknownCoordinateOrigin";
}

} // namespace an

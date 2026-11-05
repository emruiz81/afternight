// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Core/GeometryError.h>

// The word beside every reason, kept out of the header for the reason the error codes are: a switch
// over the whole enumeration compiled into every translation unit that validates an extent would be
// the geometry primitives paying for strings almost nothing reads, and one definition here means the
// word a diagnostic prints cannot differ between two translation units compiled at different times.
//
// The switch has no `default` label, so a reason added to the enumeration without a name here is a
// compile failure rather than a blank in somebody's log. The trailing return is for a value cast in
// from outside the enumeration, which is a different thing and says so.

namespace an
{

const char* geometryErrorName(GeometryError error) noexcept
{
    switch (error)
    {
    case GeometryError::None:
        return "None";
    case GeometryError::ZeroDimension:
        return "ZeroDimension";
    case GeometryError::DimensionTooLarge:
        return "DimensionTooLarge";
    case GeometryError::ChannelCountTooLarge:
        return "ChannelCountTooLarge";
    case GeometryError::PlaneCountMismatch:
        return "PlaneCountMismatch";
    case GeometryError::AlignmentNotPowerOfTwo:
        return "AlignmentNotPowerOfTwo";
    case GeometryError::ElementSizeNotPowerOfTwo:
        return "ElementSizeNotPowerOfTwo";
    case GeometryError::StrideTooSmall:
        return "StrideTooSmall";
    case GeometryError::StrideMisaligned:
        return "StrideMisaligned";
    case GeometryError::ArithmeticOverflow:
        return "ArithmeticOverflow";
    case GeometryError::SizeNotRepresentable:
        return "SizeNotRepresentable";
    case GeometryError::ImageTooLarge:
        return "ImageTooLarge";
    case GeometryError::ElementSizeMismatch:
        return "ElementSizeMismatch";
    case GeometryError::BufferTooSmall:
        return "BufferTooSmall";
    case GeometryError::BaseAddressMisaligned:
        return "BaseAddressMisaligned";
    case GeometryError::RegionOutOfBounds:
        return "RegionOutOfBounds";
    }
    return "UnknownGeometryError";
}

} // namespace an

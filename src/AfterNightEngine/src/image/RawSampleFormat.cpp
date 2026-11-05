// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/RawSampleFormat.h>

// The one name table this vocabulary needs, kept out of the header for the reason every other name
// table in this engine is: a switch compiled into every translation unit that describes a boundary
// would be the image model paying for strings almost nothing reads.
//
// The switch carries no `default` label, so a class added to the enumeration without a name here is
// a compile failure rather than a blank in somebody's log. The trailing return is for a value cast
// in from outside the enumeration, which is a different thing and says so.

namespace an
{

const char* rawSampleClassName(RawSampleClass sampleClass) noexcept
{
    switch (sampleClass)
    {
    case RawSampleClass::UnsignedInteger:
        return "UnsignedInteger";
    case RawSampleClass::SignedInteger:
        return "SignedInteger";
    case RawSampleClass::FloatingPoint:
        return "FloatingPoint";
    case RawSampleClass::Opaque:
        return "Opaque";
    }
    return "UnknownRawSampleClass";
}

} // namespace an

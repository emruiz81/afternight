// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/SampleType.h>

// The names of the eight representations and the three byte orders. Everything else about them is a
// constant expression in the header, because a size and a signedness are read inside decisions the
// compiler should be able to fold; a name is read by a person, so it costs a call and lives here.
//
// Neither switch has a `default` label, so a representation added to the enumeration without a name
// is a compile failure rather than a blank in a log.

namespace an
{

const char* sampleTypeName(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::UInt8:
        return "UInt8";
    case SampleType::Int8:
        return "Int8";
    case SampleType::UInt16:
        return "UInt16";
    case SampleType::Int16:
        return "Int16";
    case SampleType::UInt32:
        return "UInt32";
    case SampleType::Int32:
        return "Int32";
    case SampleType::Float32:
        return "Float32";
    case SampleType::Float64:
        return "Float64";
    }
    return "UnknownSampleType";
}

const char* byteOrderName(ByteOrder order) noexcept
{
    switch (order)
    {
    case ByteOrder::Native:
        return "Native";
    case ByteOrder::Little:
        return "Little";
    case ByteOrder::Big:
        return "Big";
    }
    return "UnknownByteOrder";
}

} // namespace an

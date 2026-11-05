// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ChannelSemantics.h>

// The names of the three vocabularies. The rules that judge them are constant expressions in the
// header, because a descriptor's validation should fold away where the values are known; the names
// are read by a person and by the text form, so they cost a call and live here.
//
// None of these switches has a `default` label, so a value added to any of the three enumerations
// without a name is a compile failure rather than a blank in a log.

namespace an
{

const char* channelSchemaName(ChannelSchema schema) noexcept
{
    switch (schema)
    {
    case ChannelSchema::Mono:
        return "Mono";
    case ChannelSchema::Rgb:
        return "Rgb";
    case ChannelSchema::Rgba:
        return "Rgba";
    case ChannelSchema::Xyz:
        return "Xyz";
    case ChannelSchema::Lab:
        return "Lab";
    case ChannelSchema::Lch:
        return "Lch";
    case ChannelSchema::Hsl:
        return "Hsl";
    case ChannelSchema::Hsv:
        return "Hsv";
    case ChannelSchema::Hsi:
        return "Hsi";
    case ChannelSchema::YCbCr:
        return "YCbCr";
    case ChannelSchema::Named:
        return "Named";
    }
    return "UnknownChannelSchema";
}

const char* colorSpaceName(ColorSpace space) noexcept
{
    switch (space)
    {
    case ColorSpace::Unspecified:
        return "Unspecified";
    case ColorSpace::LinearRgb:
        return "LinearRgb";
    case ColorSpace::DisplayRgb:
        return "DisplayRgb";
    case ColorSpace::CieXyz:
        return "CieXyz";
    case ColorSpace::CieLab:
        return "CieLab";
    case ColorSpace::NarrowbandMapping:
        return "NarrowbandMapping";
    }
    return "UnknownColorSpace";
}

const char* numericDomainName(NumericDomain domain) noexcept
{
    switch (domain)
    {
    case NumericDomain::NormalizedLinear:
        return "NormalizedLinear";
    case NumericDomain::EncodedNonlinear:
        return "EncodedNonlinear";
    case NumericDomain::RawSensorCode:
        return "RawSensorCode";
    case NumericDomain::PhysicalScaled:
        return "PhysicalScaled";
    }
    return "UnknownNumericDomain";
}

} // namespace an

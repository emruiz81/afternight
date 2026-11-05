// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/DescriptorError.h>

// The text beside every reason, kept out of the header for the reason the error codes are: a switch
// over the whole enumeration compiled into every translation unit that validates a descriptor would
// be the image model paying for strings almost nothing reads, and one definition here means the
// word a diagnostic prints cannot differ between two translation units compiled at different times.
//
// Neither switch has a `default` label, so a reason added to the enumeration without a name and a
// sentence here is a compile failure rather than a blank in somebody's log. The trailing return
// after each switch is for a value cast in from outside the enumeration, which is a different thing
// and says so.

namespace an
{

const char* descriptorErrorName(DescriptorError error) noexcept
{
    switch (error)
    {
    case DescriptorError::None:
        return "None";
    case DescriptorError::UnknownSampleType:
        return "UnknownSampleType";
    case DescriptorError::UnknownChannelSchema:
        return "UnknownChannelSchema";
    case DescriptorError::UnknownColorSpace:
        return "UnknownColorSpace";
    case DescriptorError::UnknownNumericDomain:
        return "UnknownNumericDomain";
    case DescriptorError::UnknownCfaPattern:
        return "UnknownCfaPattern";
    case DescriptorError::UnknownCoordinateOrigin:
        return "UnknownCoordinateOrigin";
    case DescriptorError::ChannelCountMismatch:
        return "ChannelCountMismatch";
    case DescriptorError::ColorSpaceMismatch:
        return "ColorSpaceMismatch";
    case DescriptorError::NumericDomainMismatch:
        return "NumericDomainMismatch";
    case DescriptorError::NumericDomainNotRepresentable:
        return "NumericDomainNotRepresentable";
    case DescriptorError::CfaOriginOutOfPattern:
        return "CfaOriginOutOfPattern";
    case DescriptorError::CfaOriginNotZero:
        return "CfaOriginNotZero";
    case DescriptorError::CfaRequiresSingleChannel:
        return "CfaRequiresSingleChannel";
    case DescriptorError::CfaRequiresMonoSchema:
        return "CfaRequiresMonoSchema";
    case DescriptorError::CfaTableRequired:
        return "CfaTableRequired";
    case DescriptorError::CfaTableNotAllowed:
        return "CfaTableNotAllowed";
    case DescriptorError::UnknownCfaColor:
        return "UnknownCfaColor";
    case DescriptorError::CfaTableColorCountsWrong:
        return "CfaTableColorCountsWrong";
    case DescriptorError::CfaTableLineCountsWrong:
        return "CfaTableLineCountsWrong";
    case DescriptorError::CfaTableGreenNeighbourhoodWrong:
        return "CfaTableGreenNeighbourhoodWrong";
    case DescriptorError::PixelAspectNotPositive:
        return "PixelAspectNotPositive";
    case DescriptorError::ExtentRefused:
        return "ExtentRefused";
    case DescriptorError::MalformedText:
        return "MalformedText";
    case DescriptorError::UnknownField:
        return "UnknownField";
    case DescriptorError::MissingField:
        return "MissingField";
    case DescriptorError::DuplicateField:
        return "DuplicateField";
    case DescriptorError::UnknownRawSampleClass:
        return "UnknownRawSampleClass";
    case DescriptorError::RawSampleSizeNotSupported:
        return "RawSampleSizeNotSupported";
    case DescriptorError::UnknownByteOrder:
        return "UnknownByteOrder";
    case DescriptorError::ByteOrderNotMeaningful:
        return "ByteOrderNotMeaningful";
    case DescriptorError::UnknownValidityRule:
        return "UnknownValidityRule";
    case DescriptorError::ValidityRangeMissing:
        return "ValidityRangeMissing";
    case DescriptorError::ValidityRangeNotAllowed:
        return "ValidityRangeNotAllowed";
    case DescriptorError::ValidityRangeNotFinite:
        return "ValidityRangeNotFinite";
    case DescriptorError::ValidityRangeNotOrdered:
        return "ValidityRangeNotOrdered";
    }
    return "UnknownDescriptorError";
}

const char* descriptorErrorDescription(DescriptorError error) noexcept
{
    switch (error)
    {
    case DescriptorError::None:
        return "the values describe an image this engine can represent";
    case DescriptorError::UnknownSampleType:
        return "the sample type is not one of the eight representations this engine stores";
    case DescriptorError::UnknownChannelSchema:
        return "the channel schema is not one this engine knows";
    case DescriptorError::UnknownColorSpace:
        return "the colour space is not one this engine knows";
    case DescriptorError::UnknownNumericDomain:
        return "the numeric domain is not one this engine knows";
    case DescriptorError::UnknownCfaPattern:
        return "the colour filter array pattern is not one this engine can describe";
    case DescriptorError::UnknownCoordinateOrigin:
        return "the coordinate origin is neither of the two row orders this engine reads";
    case DescriptorError::ChannelCountMismatch:
        return "the channel schema fixes a channel count and the extent states a different one";
    case DescriptorError::ColorSpaceMismatch:
        return "the channel schema and the colour space cannot describe the same values";
    case DescriptorError::NumericDomainMismatch:
        return "the colour space and the numeric domain contradict each other";
    case DescriptorError::NumericDomainNotRepresentable:
        return "the sample type cannot carry values in this numeric domain";
    case DescriptorError::CfaOriginOutOfPattern:
        return "the colour filter array phase is at or beyond the block it indexes";
    case DescriptorError::CfaOriginNotZero:
        return "a colour filter array phase was given for an image that has no mosaic";
    case DescriptorError::CfaRequiresSingleChannel:
        return "a colour filter array was given for an image with more than one channel";
    case DescriptorError::CfaRequiresMonoSchema:
        return "a colour filter array was given for an image whose channel is not plain intensity";
    case DescriptorError::CfaTableRequired:
        return "a tabulated colour filter array was named without the table its filters live in";
    case DescriptorError::CfaTableNotAllowed:
        return "a filter table was carried by a mosaic whose pattern already fixes its filters";
    case DescriptorError::UnknownCfaColor:
        return "an entry of the filter table is not one of the filter colours this engine knows";
    case DescriptorError::CfaTableColorCountsWrong:
        return "the filter table does not hold eight red, twenty green and eight blue filters";
    case DescriptorError::CfaTableLineCountsWrong:
        return "a row or column of the filter table breaks the red and blue counts a mosaic has";
    case DescriptorError::CfaTableGreenNeighbourhoodWrong:
        return "a green filter of the table has a neighbourhood no green filter of the mosaic has";
    case DescriptorError::PixelAspectNotPositive:
        return "the pixel aspect ratio is not a positive finite number";
    case DescriptorError::ExtentRefused:
        return "the dimensions were read but build no extent under the limits in force";
    case DescriptorError::MalformedText:
        return "the text is not the space-separated key=value form this engine writes";
    case DescriptorError::UnknownField:
        return "the text carries a field this descriptor does not have";
    case DescriptorError::MissingField:
        return "the text leaves out a field, and no field has a default";
    case DescriptorError::DuplicateField:
        return "the text carries one field more than once";
    case DescriptorError::UnknownRawSampleClass:
        return "the class of value a raw sample holds is not one this engine describes";
    case DescriptorError::RawSampleSizeNotSupported:
        return "a raw sample is not a power-of-two width this engine will describe";
    case DescriptorError::UnknownByteOrder:
        return "the byte order is not one this engine reads";
    case DescriptorError::ByteOrderNotMeaningful:
        return "a byte order was stated for a sample that occupies one byte";
    case DescriptorError::UnknownValidityRule:
        return "the validity rule is not one this engine knows";
    case DescriptorError::ValidityRangeMissing:
        return "a validity rule that decides by a range was given without one";
    case DescriptorError::ValidityRangeNotAllowed:
        return "a validity range was given for a rule that decides some other way";
    case DescriptorError::ValidityRangeNotFinite:
        return "a bound of the validity range is not a finite number";
    case DescriptorError::ValidityRangeNotOrdered:
        return "the validity range's lowest bound is above its highest";
    }
    return "the reason is not one this build knows, which is what a reason from a newer build looks like";
}

} // namespace an

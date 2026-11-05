// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - names for the reasons a pointwise program is refused.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramError.h>

namespace an
{

const char* pointwiseProgramErrorName(PointwiseProgramError error) noexcept
{
    switch (error)
    {
    case PointwiseProgramError::None:
        return "none";
    case PointwiseProgramError::EmptyProgram:
        return "emptyProgram";
    case PointwiseProgramError::UnknownOpKind:
        return "unknownOpKind";
    case PointwiseProgramError::NonFiniteCoefficient:
        return "nonFiniteCoefficient";
    case PointwiseProgramError::UnusedFieldSet:
        return "unusedFieldSet";
    case PointwiseProgramError::InvertedClampBounds:
        return "invertedClampBounds";
    case PointwiseProgramError::BlendWeightOutOfRange:
        return "blendWeightOutOfRange";
    case PointwiseProgramError::UnknownComparison:
        return "unknownComparison";
    case PointwiseProgramError::UnknownCurveSampling:
        return "unknownCurveSampling";
    case PointwiseProgramError::UnknownCurveDomainPolicy:
        return "unknownCurveDomainPolicy";
    case PointwiseProgramError::InvalidCurveDomain:
        return "invalidCurveDomain";
    case PointwiseProgramError::OperandOutOfRange:
        return "operandOutOfRange";
    case PointwiseProgramError::TableOutOfRange:
        return "tableOutOfRange";
    case PointwiseProgramError::TableSizeMismatch:
        return "tableSizeMismatch";
    case PointwiseProgramError::NonFiniteTableEntry:
        return "nonFiniteTableEntry";
    case PointwiseProgramError::LutNotFirstStep:
        return "lutNotFirstStep";
    case PointwiseProgramError::LutSourceNotIndexable:
        return "lutSourceNotIndexable";
    case PointwiseProgramError::ChannelCountUnsupported:
        return "channelCountUnsupported";
    case PointwiseProgramError::SourceCountUnsupported:
        return "sourceCountUnsupported";
    case PointwiseProgramError::TooManySteps:
        return "tooManySteps";
    case PointwiseProgramError::ComputePrecisionRefused:
        return "computePrecisionRefused";
    case PointwiseProgramError::NumericDomainRefused:
        return "numericDomainRefused";
    }
    return "unknown";
}

} // namespace an

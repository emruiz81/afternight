// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/ComputePrecision.h>

// Four name tables and nothing else: every rule in this header is a constant expression, which is
// what lets an operation state its precision at compile time and a plan check one at run time
// against the same code.
//
// None of the switches carries a `default` label, so an enumerator added without a name here is a
// compile failure rather than a blank in somebody's log. Each trailing return is for a value cast
// in from outside its enumeration, which is a different thing and says so.

namespace an
{

const char* computePrecisionName(ComputePrecision precision) noexcept
{
    switch (precision)
    {
    case ComputePrecision::Float32:
        return "Float32";
    case ComputePrecision::Float64:
        return "Float64";
    }
    return "UnknownComputePrecision";
}

const char* outputPrecisionContractName(OutputPrecisionContract contract) noexcept
{
    switch (contract)
    {
    case OutputPrecisionContract::ExactStoredValues:
        return "ExactStoredValues";
    case OutputPrecisionContract::NormalizedOrLossy:
        return "NormalizedOrLossy";
    }
    return "UnknownOutputPrecisionContract";
}

const char* float64JustificationName(Float64Justification justification) noexcept
{
    switch (justification)
    {
    case Float64Justification::StoredInFloat64:
        return "StoredInFloat64";
    case Float64Justification::ExactIntegerCodeValues:
        return "ExactIntegerCodeValues";
    case Float64Justification::CalibrationDivide:
        return "CalibrationDivide";
    case Float64Justification::StackingAccumulation:
        return "StackingAccumulation";
    case Float64Justification::ReductionAccumulator:
        return "ReductionAccumulator";
    case Float64Justification::AstrometricFitting:
        return "AstrometricFitting";
    }
    return "UnknownFloat64Justification";
}

const char* precisionErrorName(PrecisionError error) noexcept
{
    switch (error)
    {
    case PrecisionError::None:
        return "None";
    case PrecisionError::UnknownSampleType:
        return "UnknownSampleType";
    case PrecisionError::UnknownComputePrecision:
        return "UnknownComputePrecision";
    case PrecisionError::UnknownOutputContract:
        return "UnknownOutputContract";
    case PrecisionError::UnknownJustification:
        return "UnknownJustification";
    case PrecisionError::Float64NeedsJustification:
        return "Float64NeedsJustification";
    case PrecisionError::JustificationWithoutFloat64:
        return "JustificationWithoutFloat64";
    case PrecisionError::JustificationNotApplicable:
        return "JustificationNotApplicable";
    case PrecisionError::PrecisionBelowContract:
        return "PrecisionBelowContract";
    }
    return "UnknownPrecisionError";
}

} // namespace an

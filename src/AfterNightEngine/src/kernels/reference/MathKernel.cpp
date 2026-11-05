// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>

#include "kernels/reference/AlgorithmKernels.h"

#include <cmath>
#include <limits>

// The authoritative scalar transcendentals, the policy that decides who evaluates one, and the
// bound a vector implementation is held to.
//
// Everything here is deliberately unremarkable arithmetic: the platform's math library, called once
// per sample in Float64 and narrowed once on the way out. That is what makes it the reference. The
// two things worth reading are above it rather than in it.
//
// **The exceptional inputs are decided here and copied nowhere.** A vector kernel cannot afford to
// branch per lane, so it computes its polynomial for every lane and then blends the exceptional
// answers over the top - and the list of answers it blends has to be the same list this file
// produces, or the two implementations disagree at exactly the inputs nobody generates by accident.
// Writing them once, as ordinary scalar code that a person can read, is what keeps that list one
// list.
//
// **`pow` is narrower than `std::pow`, and the reference is narrowed with it.** This engine's `pow`
// is a transfer function: a non-negative intensity raised to a stated exponent. `std::pow` also
// answers for a negative base and an integral exponent, which no tone curve, gamma or display
// encoding asks for and which a vector composition of an exponential and a logarithm cannot produce
// without a second implementation inside it. So the domain is stated, the reference applies the
// same rule, and a negative base is a quiet not-a-number in both.

namespace an
{
namespace
{

/// A quiet not-a-number, which is what every out-of-domain evaluation produces.
///
/// @return the quiet not-a-number of the Float32 representation.
[[nodiscard]] float quietNotANumber() noexcept
{
    return std::numeric_limits<float>::quiet_NaN();
}

/// The value of this engine's `pow` at one non-negative base.
///
/// @param value the base, already known to be neither negative nor a not-a-number.
/// @param exponent the finite exponent.
/// @return the narrowed result.
[[nodiscard]] float powValue(float value, double exponent) noexcept
{
    // std::pow already answers correctly for a zero or infinite base once the sign of the exponent
    // is taken into account, and for an exponent of zero, so the domain guard above it is the whole
    // of the narrowing.
    return static_cast<float>(std::pow(static_cast<double>(value), exponent));
}

/// How many code values a representation has, when a table over all of them is worth building.
///
/// @param sampleType the resident representation.
/// @return the entry count, or zero for a representation this engine evaluates rather than
///         tabulates.
[[nodiscard]] std::uint32_t tableEntriesFor(SampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case SampleType::UInt8:
    case SampleType::Int8:
        return 256u;
    case SampleType::UInt16:
    case SampleType::Int16:
        return kMaxMathTableEntries;
    case SampleType::UInt32:
    case SampleType::Int32:
    case SampleType::Float32:
    case SampleType::Float64:
        // Listed rather than covered by a default label, so a representation added to the closed
        // set fails this switch on a compiler that reports an unhandled case instead of silently
        // acquiring a four-thousand-megabyte table.
        return 0u;
    }
    return 0u;
}

} // namespace

const char* mathFunctionName(MathFunction function) noexcept
{
    switch (function)
    {
    case MathFunction::Exp:
        return "Exp";
    case MathFunction::Log:
        return "Log";
    case MathFunction::Pow:
        return "Pow";
    case MathFunction::Unknown:
        break;
    }
    return "UnknownMathFunction";
}

const char* mathProviderName(MathProvider provider) noexcept
{
    switch (provider)
    {
    case MathProvider::ExactTable:
        return "ExactTable";
    case MathProvider::VectorApproximation:
        return "VectorApproximation";
    case MathProvider::ScalarLibrary:
        return "ScalarLibrary";
    case MathProvider::Unknown:
        break;
    }
    return "UnknownMathProvider";
}

MathProvider selectMathProvider(const MathProviderRequest& request) noexcept
{
    if (!isMathFunction(request.function) || !isSampleType(request.inputSample))
    {
        return MathProvider::Unknown;
    }
    if (request.precision == ComputePrecision::Float64 || request.inputSample == SampleType::Float64)
    {
        // A caller that asked for the wider arithmetic asked for it because the narrower answer was
        // not good enough, and a Float64 sample put through Float32 polynomials would lose exactly
        // what made it a Float64 sample.
        return MathProvider::ScalarLibrary;
    }
    const std::uint32_t entries = tableEntriesFor(request.inputSample);
    if (entries != 0u && (request.sampleCount == 0u || request.sampleCount >= entries))
    {
        return MathProvider::ExactTable;
    }
    return MathProvider::VectorApproximation;
}

float referenceMathValue(MathFunction function, float value, double exponent) noexcept
{
    if (std::isnan(value))
    {
        return quietNotANumber();
    }
    switch (function)
    {
    case MathFunction::Exp:
        return static_cast<float>(std::exp(static_cast<double>(value)));
    case MathFunction::Log:
        if (value < 0.0f)
        {
            return quietNotANumber();
        }
        return static_cast<float>(std::log(static_cast<double>(value)));
    case MathFunction::Pow:
        if (value < 0.0f || !std::isfinite(exponent))
        {
            return quietNotANumber();
        }
        return powValue(value, exponent);
    case MathFunction::Unknown:
        break;
    }
    return quietNotANumber();
}

double mathUlpBound(MathFunction function, float value, double exponent) noexcept
{
    if (!isMathFunction(function))
    {
        return 0.0;
    }
    const float reference = referenceMathValue(function, value, exponent);
    if (!std::isfinite(reference) || std::abs(reference) < (std::numeric_limits<float>::min)())
    {
        // An exceptional or subnormal result is compared by class rather than by distance: a
        // subnormal is a handful of representable values away from zero, so a distance in units in
        // the last place says nothing there, and an infinity has no distance at all.
        return 0.0;
    }
    if (function != MathFunction::Pow)
    {
        return kVectorMathUlpTier;
    }
    const double argument = exponent * std::log(static_cast<double>(value));
    if (!std::isfinite(argument))
    {
        return 0.0;
    }
    return kVectorMathUlpTier + kComposedMathUlpPerArgument * std::abs(argument);
}

void referenceMathRun(MathFunction function, const float* source, float* destination, std::uint64_t count,
                      double exponent) noexcept
{
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        destination[index] = referenceMathValue(function, source[index], exponent);
    }
}

} // namespace an

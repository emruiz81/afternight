// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a stack method's output means, and which column kernel combines a sample history.
///
/// Two decisions follow from the checked model alone and are needed by more than the kernels:
/// the numeric domain and the description of the output, which `executeStack` hands over and the
/// sum domain can change, and whether a sample-history stack is combined by the median or the
/// auto-adaptive column kernel. They are constant expressions over the full `StackModel`, kept
/// apart from `Algorithms/Stacking/StackMethods.h` so the kernel interface does not include the
/// model.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackMethods.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>

namespace an
{

/// The numeric domain a method's output carries.
///
/// Every combination but the sum keeps the input's domain, because a mean, a median, an extremum
/// and the auto-adaptive mean of fractions are fractions. The sum's row is a table over the input's
/// domain: normalized to its peak, the output is a fraction of full scale for every linear input and
/// stays an encoded fraction for an encoded one; raw, the output is `NumericDomain::PhysicalScaled`
/// for every linear input - a linear quantity in the input's units, which a floating representation
/// carries - and a raw sum of nonlinearly encoded samples is neither, so that cell is empty.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param sumDomain what a sum's sums mean; inert for every other combination.
/// @return the output's domain, or an empty optional for a value outside `StackSumDomain` and for
///         the raw sum of an encoded input.
[[nodiscard]] constexpr std::optional<NumericDomain> stackMethodOutputDomain(const StackModel& model,
                                                                             StackSumDomain sumDomain) noexcept
{
    if (!isStackSumDomain(sumDomain))
    {
        return std::nullopt;
    }
    const NumericDomain input = model.request().input.descriptor.numericDomain();
    if (model.request().combination != StackCombination::Sum)
    {
        return input;
    }
    switch (input)
    {
    case NumericDomain::NormalizedLinear:
    case NumericDomain::RawSensorCode:
    case NumericDomain::PhysicalScaled:
        return sumDomain == StackSumDomain::NormalizedToPeak ? NumericDomain::NormalizedLinear
                                                             : NumericDomain::PhysicalScaled;
    case NumericDomain::EncodedNonlinear:
        if (sumDomain == StackSumDomain::NormalizedToPeak)
        {
            return NumericDomain::EncodedNonlinear;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

/// The description of a method's output.
///
/// The model's output descriptor with the numeric domain replaced by `stackMethodOutputDomain`,
/// which is the one member a method restates: the representation is the request's, and the
/// extent, schema, basis, filter array, origin and aspect are the frames'.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @param sumDomain what a sum's sums mean; inert for every other combination.
/// @return the descriptor, or an empty optional exactly when `stackMethodOutputDomain` is empty.
[[nodiscard]] constexpr std::optional<ImageDescriptor> stackMethodOutputDescriptor(const StackModel& model,
                                                                                   StackSumDomain sumDomain) noexcept
{
    const std::optional<NumericDomain> domain = stackMethodOutputDomain(model, sumDomain);
    if (!domain.has_value())
    {
        return std::nullopt;
    }
    ImageDescriptorRequest output = model.outputDescriptor().toRequest();
    output.numericDomain = *domain;
    return ImageDescriptor::create(output);
}

/// Which column kernel of `combineStackSampleHistory` combines a model's sample history, if one
/// does.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param model the checked stack.
/// @return `Median` or `AutoAdaptiveWeightedAverage` for a stack of that combination with no
///         rejection; an empty optional for a streaming combination, whose samples are combined by
///         the whole-output accumulators, and for any rejection, whose survivors are combined by
///         its estimator.
[[nodiscard]] constexpr std::optional<StackSampleHistoryCombination>
stackSampleHistoryCombinationOf(const StackModel& model) noexcept
{
    if (model.usesRejection())
    {
        return std::nullopt;
    }
    switch (model.request().combination)
    {
    case StackCombination::Median:
        return StackSampleHistoryCombination::Median;
    case StackCombination::AutoAdaptiveWeightedAverage:
        return StackSampleHistoryCombination::AutoAdaptiveWeightedAverage;
    case StackCombination::Average:
    case StackCombination::Sum:
    case StackCombination::Minimum:
    case StackCombination::Maximum:
        break;
    }
    return std::nullopt;
}

} // namespace an

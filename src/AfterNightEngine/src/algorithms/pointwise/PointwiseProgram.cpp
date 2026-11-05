// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - when two validated pointwise chains are the same chain.
//
// Equality compares everything, including the tables and every declaration, because this is what
// the canonicalization tests assert against. A comparison that ignored the declared width would
// call two programs equal when one of them memoizes arithmetic the other performs differently.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>

#include <cstddef>

namespace an
{

bool operator==(const PointwiseProgram& left, const PointwiseProgram& right) noexcept
{
    if (left.channelCount() != right.channelCount() || left.sourceCount() != right.sourceCount() ||
        left.sourceSampleType() != right.sourceSampleType() || left.precision() != right.precision() ||
        left.inputDomain() != right.inputDomain() || left.outputDomain() != right.outputDomain() ||
        left.precisionJustification() != right.precisionJustification() ||
        left.outputContract() != right.outputContract() || left.contractionAllowed() != right.contractionAllowed())
    {
        return false;
    }

    const std::span<const PointwiseOp> leftSteps = left.steps();
    const std::span<const PointwiseOp> rightSteps = right.steps();
    if (leftSteps.size() != rightSteps.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < leftSteps.size(); ++index)
    {
        if (!(leftSteps[index] == rightSteps[index]))
        {
            return false;
        }
    }

    if (left.tableCount() != right.tableCount())
    {
        return false;
    }
    for (std::uint32_t table = 0u; table < left.tableCount(); ++table)
    {
        const std::span<const double> leftEntries = left.table(table);
        const std::span<const double> rightEntries = right.table(table);
        if (leftEntries.size() != rightEntries.size())
        {
            return false;
        }
        for (std::size_t entry = 0u; entry < leftEntries.size(); ++entry)
        {
            if (leftEntries[entry] != rightEntries[entry])
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace an

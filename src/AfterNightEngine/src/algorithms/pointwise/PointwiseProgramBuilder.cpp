// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - assembling a pointwise chain and deciding once whether it is executable.
//
// The order the checks run in is part of the contract rather than an implementation detail. Shape
// declarations come first because every later rule is stated in terms of them - a table size is the
// square of a channel count, an operand index is bounded by a source count - and reporting a table
// as the wrong size when the channel count was the thing that was wrong names the wrong problem.
// Within the chain, steps are checked in order and the first reason wins, so a caller fixing a
// chain fixes the earliest thing wrong with it.
//
// Within one step the order is the same shape: what the step is, then whether it said anything it
// had no business saying, then whether its numbers are numbers, then what its numbers mean. A rule
// cannot judge a step it does not understand.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

/// Whether every coefficient a step actually uses is a finite number.
[[nodiscard]] bool coefficientsAreFinite(const PointwiseOp& op) noexcept
{
    switch (op.kind)
    {
    case PointwiseOpKind::Scale:
    case PointwiseOpKind::Gamma:
    case PointwiseOpKind::Blend:
    case PointwiseOpKind::Add:
    case PointwiseOpKind::Subtract:
    case PointwiseOpKind::Multiply:
    case PointwiseOpKind::Divide:
        return std::isfinite(op.factor);
    case PointwiseOpKind::Bias:
        return std::isfinite(op.offset);
    case PointwiseOpKind::Clamp:
        return std::isfinite(op.low) && std::isfinite(op.high);
    case PointwiseOpKind::Unknown:
    case PointwiseOpKind::Lut:
    case PointwiseOpKind::ChannelMatrix:
    case PointwiseOpKind::Mask:
    case PointwiseOpKind::Minimum:
    case PointwiseOpKind::Maximum:
    case PointwiseOpKind::Compare:
        return true;
    case PointwiseOpKind::SampledCurve:
        return std::isfinite(op.low) && std::isfinite(op.high);
    }
    return true;
}

/// Whether every member the step's kind does not use is still at its neutral value.
///
/// This is what makes two steps of one kind comparable, so it is a refusal rather than something
/// the builder quietly normalizes: a chain that set a field it did not mean to set is a chain
/// assembled by code with the wrong idea of what it was building.
[[nodiscard]] bool unusedMembersAreNeutral(const PointwiseOp& op) noexcept
{
    const bool factorUsed = op.kind == PointwiseOpKind::Scale || op.kind == PointwiseOpKind::Gamma ||
                            op.kind == PointwiseOpKind::Blend || op.kind == PointwiseOpKind::Add ||
                            op.kind == PointwiseOpKind::Subtract || op.kind == PointwiseOpKind::Multiply ||
                            op.kind == PointwiseOpKind::Divide;
    const bool offsetUsed = op.kind == PointwiseOpKind::Bias;
    const bool boundsUsed = op.kind == PointwiseOpKind::Clamp || op.kind == PointwiseOpKind::SampledCurve;
    const bool operandUsed = op.kind == PointwiseOpKind::Mask || op.kind == PointwiseOpKind::Blend ||
                             op.kind == PointwiseOpKind::Add || op.kind == PointwiseOpKind::Subtract ||
                             op.kind == PointwiseOpKind::Multiply || op.kind == PointwiseOpKind::Divide ||
                             op.kind == PointwiseOpKind::Minimum || op.kind == PointwiseOpKind::Maximum ||
                             op.kind == PointwiseOpKind::Compare;
    const bool tableUsed = op.kind == PointwiseOpKind::Lut || op.kind == PointwiseOpKind::ChannelMatrix ||
                           op.kind == PointwiseOpKind::SampledCurve;
    const bool comparisonUsed = op.kind == PointwiseOpKind::Compare;
    const bool curveFieldsUsed = op.kind == PointwiseOpKind::SampledCurve;

    if (!factorUsed && op.factor != 1.0)
    {
        return false;
    }
    if (!offsetUsed && op.offset != 0.0)
    {
        return false;
    }
    if (!boundsUsed && (op.low != 0.0 || op.high != 0.0))
    {
        return false;
    }
    if (!operandUsed && op.operand != 0u)
    {
        return false;
    }
    if (!tableUsed && op.table != 0u)
    {
        return false;
    }
    if (!comparisonUsed && op.comparison != PointwiseComparison::Equal)
    {
        return false;
    }
    if (!curveFieldsUsed &&
        (op.sampling != PointwiseCurveSampling::Linear || op.domainPolicy != PointwiseCurveDomainPolicy::Clamp))
    {
        return false;
    }
    return true;
}

/// Why a chain was refused, as `build` and `release` both report it.
[[nodiscard]] Status refusedChain(PointwiseProgramError refused, std::size_t steps)
{
    return Status::failure(
        ErrorCode::InvalidArgument,
        StatusDetail{.message =
                         std::string("this pointwise chain cannot be executed: ") + pointwiseProgramErrorName(refused),
                     .operation = "PointwiseProgramBuilder::build",
                     .domain = ErrorDomain::Algorithms,
                     .context = {StatusContextField::ofText(ContextKey::Parameter, pointwiseProgramErrorName(refused)),
                                 StatusContextField::ofCount(ContextKey::Requested, steps)}});
}

} // namespace

PointwiseProgramBuilder& PointwiseProgramBuilder::channels(std::uint32_t channels) noexcept
{
    m_channelCount = channels;
    return *this;
}

PointwiseProgramBuilder& PointwiseProgramBuilder::sources(std::uint32_t sources) noexcept
{
    m_sourceCount = sources;
    return *this;
}

PointwiseProgramBuilder& PointwiseProgramBuilder::readingSamples(SampleType sampleType) noexcept
{
    m_sourceSampleType = sampleType;
    return *this;
}

PointwiseProgramBuilder& PointwiseProgramBuilder::readingDomain(NumericDomain domain) noexcept
{
    m_inputDomain = domain;
    if (!m_outputDomainExplicit)
    {
        m_outputDomain = domain;
    }
    return *this;
}

PointwiseProgramBuilder& PointwiseProgramBuilder::writingDomain(NumericDomain domain) noexcept
{
    m_outputDomain = domain;
    m_outputDomainExplicit = true;
    return *this;
}

PointwiseProgramBuilder& PointwiseProgramBuilder::promising(OutputPrecisionContract contract) noexcept
{
    m_outputContract = contract;
    return *this;
}

PointwiseProgramBuilder&
PointwiseProgramBuilder::computingIn(ComputePrecision precision,
                                     std::optional<Float64Justification> justification) noexcept
{
    m_precision = precision;
    m_justification = justification;
    return *this;
}

PointwiseProgramBuilder& PointwiseProgramBuilder::allowingContraction() noexcept
{
    m_contractionAllowed = true;
    return *this;
}

PointwiseProgramBuilder& PointwiseProgramBuilder::append(const PointwiseOp& op)
{
    m_steps.push_back(op);
    return *this;
}

std::uint32_t PointwiseProgramBuilder::addTable(std::span<const double> entries)
{
    m_tables.emplace_back(entries.begin(), entries.end());
    return static_cast<std::uint32_t>(m_tables.size() - 1u);
}

std::uint32_t PointwiseProgramBuilder::addTable(std::vector<double>&& entries)
{
    m_tables.push_back(std::move(entries));
    entries.clear();
    return static_cast<std::uint32_t>(m_tables.size() - 1u);
}

PointwiseProgramError PointwiseProgramBuilder::check() const noexcept
{
    if (m_channelCount == 0u || m_channelCount > kMaxPointwiseChannels)
    {
        return PointwiseProgramError::ChannelCountUnsupported;
    }
    if (m_sourceCount == 0u || m_sourceCount > kMaxPointwiseSources)
    {
        return PointwiseProgramError::SourceCountUnsupported;
    }
    if (!isSampleType(m_sourceSampleType) || !isComputePrecision(m_precision) ||
        !isOutputPrecisionContract(m_outputContract) ||
        checkComputePrecision(m_sourceSampleType, m_outputContract, m_precision, m_justification) !=
            PrecisionError::None)
    {
        return PointwiseProgramError::ComputePrecisionRefused;
    }
    if (!isNumericDomain(m_inputDomain) || !isNumericDomain(m_outputDomain))
    {
        return PointwiseProgramError::NumericDomainRefused;
    }
    if (m_steps.empty())
    {
        return PointwiseProgramError::EmptyProgram;
    }
    if (m_steps.size() > kMaxPointwiseSteps)
    {
        return PointwiseProgramError::TooManySteps;
    }

    for (std::size_t index = 0u; index < m_steps.size(); ++index)
    {
        const PointwiseOp& op = m_steps[index];
        if (!isPointwiseOpKind(op.kind))
        {
            return PointwiseProgramError::UnknownOpKind;
        }
        if (!unusedMembersAreNeutral(op))
        {
            return PointwiseProgramError::UnusedFieldSet;
        }
        if (!coefficientsAreFinite(op))
        {
            return PointwiseProgramError::NonFiniteCoefficient;
        }
        if (op.kind == PointwiseOpKind::Clamp && op.low > op.high)
        {
            return PointwiseProgramError::InvertedClampBounds;
        }
        if (op.kind == PointwiseOpKind::Blend && (op.factor < 0.0 || op.factor > 1.0))
        {
            return PointwiseProgramError::BlendWeightOutOfRange;
        }
        if (op.kind == PointwiseOpKind::Compare && !isPointwiseComparison(op.comparison))
        {
            return PointwiseProgramError::UnknownComparison;
        }
        if (op.kind == PointwiseOpKind::SampledCurve && !isPointwiseCurveSampling(op.sampling))
        {
            return PointwiseProgramError::UnknownCurveSampling;
        }
        if (op.kind == PointwiseOpKind::SampledCurve && !isPointwiseCurveDomainPolicy(op.domainPolicy))
        {
            return PointwiseProgramError::UnknownCurveDomainPolicy;
        }
        if (op.kind == PointwiseOpKind::SampledCurve && !(op.low < op.high))
        {
            return PointwiseProgramError::InvalidCurveDomain;
        }
        if (op.kind == PointwiseOpKind::SampledCurve && op.sampling == PointwiseCurveSampling::Lower &&
            op.domainPolicy != PointwiseCurveDomainPolicy::Clamp)
        {
            return PointwiseProgramError::InvalidCurveDomain;
        }
        if (op.kind == PointwiseOpKind::Gamma && m_inputDomain != NumericDomain::NormalizedLinear &&
            m_inputDomain != NumericDomain::EncodedNonlinear)
        {
            return PointwiseProgramError::NumericDomainRefused;
        }
        const bool readsOperand = op.kind == PointwiseOpKind::Mask || op.kind == PointwiseOpKind::Blend ||
                                  op.kind == PointwiseOpKind::Add || op.kind == PointwiseOpKind::Subtract ||
                                  op.kind == PointwiseOpKind::Multiply || op.kind == PointwiseOpKind::Divide ||
                                  op.kind == PointwiseOpKind::Minimum || op.kind == PointwiseOpKind::Maximum ||
                                  op.kind == PointwiseOpKind::Compare;
        if (readsOperand && op.operand >= m_sourceCount)
        {
            return PointwiseProgramError::OperandOutOfRange;
        }

        const bool readsTable = op.kind == PointwiseOpKind::Lut || op.kind == PointwiseOpKind::ChannelMatrix ||
                                op.kind == PointwiseOpKind::SampledCurve;
        if (readsTable && op.table >= m_tables.size())
        {
            return PointwiseProgramError::TableOutOfRange;
        }

        if (op.kind == PointwiseOpKind::Lut)
        {
            if (index != 0u)
            {
                return PointwiseProgramError::LutNotFirstStep;
            }
            const std::size_t required = pointwiseLutSizeFor(m_sourceSampleType);
            if (required == 0u)
            {
                return PointwiseProgramError::LutSourceNotIndexable;
            }
            if (m_tables[op.table].size() != required)
            {
                return PointwiseProgramError::TableSizeMismatch;
            }
        }
        if (op.kind == PointwiseOpKind::ChannelMatrix)
        {
            const std::size_t required = static_cast<std::size_t>(m_channelCount) * m_channelCount;
            if (m_tables[op.table].size() != required)
            {
                return PointwiseProgramError::TableSizeMismatch;
            }
        }
        if (op.kind == PointwiseOpKind::SampledCurve && m_tables[op.table].size() < 2u)
        {
            return PointwiseProgramError::TableSizeMismatch;
        }
        if (readsTable)
        {
            for (const double entry : m_tables[op.table])
            {
                if (!std::isfinite(entry))
                {
                    return PointwiseProgramError::NonFiniteTableEntry;
                }
            }
        }
    }

    return PointwiseProgramError::None;
}

Result<PointwiseProgram> PointwiseProgramBuilder::build() const
{
    const PointwiseProgramError refused = check();
    if (refused != PointwiseProgramError::None)
    {
        return refusedChain(refused, m_steps.size());
    }

    PointwiseProgram program;
    program.m_steps = m_steps;
    program.m_tables = m_tables;
    program.m_channelCount = m_channelCount;
    program.m_sourceCount = m_sourceCount;
    program.m_sourceSampleType = m_sourceSampleType;
    program.m_inputDomain = m_inputDomain;
    program.m_outputDomain = m_outputDomain;
    program.m_precision = m_precision;
    program.m_precisionJustification = m_justification;
    program.m_outputContract = m_outputContract;
    program.m_contractionAllowed = m_contractionAllowed;
    return program;
}

Result<PointwiseProgram> PointwiseProgramBuilder::release()
{
    const PointwiseProgramError refused = check();
    if (refused != PointwiseProgramError::None)
    {
        return refusedChain(refused, m_steps.size());
    }

    PointwiseProgram program;
    program.m_steps = std::move(m_steps);
    program.m_tables = std::move(m_tables);
    m_steps.clear();
    m_tables.clear();
    program.m_channelCount = m_channelCount;
    program.m_sourceCount = m_sourceCount;
    program.m_sourceSampleType = m_sourceSampleType;
    program.m_inputDomain = m_inputDomain;
    program.m_outputDomain = m_outputDomain;
    program.m_precision = m_precision;
    program.m_precisionJustification = m_justification;
    program.m_outputContract = m_outputContract;
    program.m_contractionAllowed = m_contractionAllowed;
    return program;
}

} // namespace an

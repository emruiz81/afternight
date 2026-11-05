// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The only way to produce a validated pointwise program.
///
/// A builder rather than a request record, because a chain is variable length and the thing being
/// assembled has ownership: the tables a chain reads are produced while it is assembled and belong
/// to the finished program. What it borrows from the request idiom used elsewhere in this engine is
/// the shape that matters - `check` returns the reason and nothing else, `build` returns the program
/// and a full failure - so a caller that only wants to know whether a chain is legal does not have
/// to construct one and throw it away.
///
/// The builder validates nothing as it goes. Appending a step with a coefficient that is not a
/// number succeeds, and the refusal arrives at `check` or `build`. That is deliberate: a chain is
/// often assembled by code that is itself deciding what the coefficients are, and a builder that
/// refused halfway would force every such caller to unwind. It also keeps every rule in one place,
/// applied in one fixed order, so the reason a caller is given is the first thing wrong with the
/// chain. That reason names what was wrong, not which step: neither `check` nor `build` reports the
/// index of the step that failed.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramError.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

/// Assembles a chain of element-wise steps and validates it once.
///
/// Not thread-safe: one builder belongs to whatever is assembling one chain. The program it
/// produces is immutable and may then be shared freely.
class PointwiseProgramBuilder
{
public:
    /// Starts an empty chain over one single-channel Float32 source in the normalized linear domain,
    /// computing in Float32 with no contraction and promising `NormalizedOrLossy` output.
    PointwiseProgramBuilder() = default;

    /// States how many channels one pixel carries.
    ///
    /// Not checked here: `check` refuses a count outside one through `kMaxPointwiseChannels`.
    ///
    /// @param channels the channel count.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& channels(std::uint32_t channels) noexcept;

    /// States how many source planes an execution will bind, including the primary input.
    ///
    /// Not checked here: `check` refuses a count outside one through `kMaxPointwiseSources`.
    ///
    /// @param sources the source count.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& sources(std::uint32_t sources) noexcept;

    /// States what the primary source's samples are.
    ///
    /// @param sampleType the representation.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& readingSamples(SampleType sampleType) noexcept;

    /// States what the values supplied by every source mean.
    ///
    /// All sources of one program share one domain. Binary arithmetic therefore never infers a
    /// normalization from either representation: operands already mean the same kind of number or
    /// they are not inputs to the same program. Unless `writingDomain` has already been called, the
    /// output domain follows this one.
    ///
    /// @param domain the input numeric domain.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& readingDomain(NumericDomain domain) noexcept;

    /// States what the values produced by the chain mean.
    ///
    /// This is the program's declared metadata transition. A plain arithmetic chain normally keeps
    /// the input domain; a gamma encoding or curve normally writes `EncodedNonlinear`.
    ///
    /// @param domain the output numeric domain.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& writingDomain(NumericDomain domain) noexcept;

    /// States what the chain promises about the values it writes.
    ///
    /// @param contract the output contract.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& promising(OutputPrecisionContract contract) noexcept;

    /// States the width the arithmetic runs at, and why if it is Float64.
    ///
    /// The pair is checked together against the source representation and the output contract by
    /// the same rule every other operation in this engine uses, so a chain cannot reach Float64 by
    /// a route the rest of the engine has closed; `check` reports a refusal as
    /// `PointwiseProgramError::ComputePrecisionRefused`.
    ///
    /// @param precision the arithmetic width.
    /// @param justification why, required for Float64 and ignored for Float32.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder&
    computingIn(ComputePrecision precision, std::optional<Float64Justification> justification = std::nullopt) noexcept;

    /// Permits a rewrite to replace two roundings with one.
    ///
    /// Off unless asked for. What it buys is a fused multiply-add where the chain scales and then
    /// biases; what it costs is that the result is no longer bit-identical to running the steps one
    /// at a time, which is the comparison the conformance harness makes.
    ///
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& allowingContraction() noexcept;

    /// Appends one step to the chain.
    ///
    /// @param op the step.
    /// @return this builder, for chaining.
    AFTERNIGHTENGINE_API PointwiseProgramBuilder& append(const PointwiseOp& op);

    /// Adds a table the chain's steps may read, and says where it landed.
    ///
    /// The entries are copied, so the caller's storage is not borrowed. Tables are numbered in the
    /// order they are added.
    ///
    /// @param entries the table's values.
    /// @return the index a `Lut`, `ChannelMatrix` or `SampledCurve` step should name.
    AFTERNIGHTENGINE_API std::uint32_t addTable(std::span<const double> entries);

    /// Adds a table the chain's steps may read by taking the caller's entries over rather than copying them.
    ///
    /// The same table `addTable(std::span<const double>)` adds, for a caller that made its entries for this program
    /// and has no further use for them: a curve's table is half a mebibyte.
    ///
    /// @param entries the table's values; left empty.
    /// @return the index a `Lut`, `ChannelMatrix` or `SampledCurve` step should name.
    AFTERNIGHTENGINE_API std::uint32_t addTable(std::vector<double>&& entries);

    /// How many steps have been appended so far.
    ///
    /// @return the step count.
    [[nodiscard]] std::uint32_t stepCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_steps.size());
    }

    /// Why the chain as it stands would be refused.
    ///
    /// Checks facts before combinations and reports the first reason in step order, so a caller
    /// fixing a chain fixes the earliest thing wrong with it rather than whichever rule happened to
    /// be evaluated first.
    ///
    /// Thread-safe with respect to other readers of this builder, and allocation-free.
    ///
    /// @return `PointwiseProgramError::None` when the chain would be accepted.
    [[nodiscard]] AFTERNIGHTENGINE_API PointwiseProgramError check() const noexcept;

    /// The validated program, or why there is not one.
    ///
    /// @return the program, or `ErrorCode::InvalidArgument` carrying the reason `check` would have
    ///         given, in the failure's message and as `ContextKey::Parameter`, with the chain's step
    ///         count as `ContextKey::Requested`.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> build() const;

    /// The validated program `build` would return, with this builder's steps and tables moved into it rather than
    /// copied.
    ///
    /// For a builder that is done: on success it is left holding no steps and no tables, its other settings as they
    /// were; on a refusal it is unchanged.
    ///
    /// @return what `build` returns.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<PointwiseProgram> release();

private:
    std::vector<PointwiseOp> m_steps;
    std::vector<std::vector<double>> m_tables;
    std::uint32_t m_channelCount = 1u;
    std::uint32_t m_sourceCount = 1u;
    SampleType m_sourceSampleType = SampleType::Float32;
    NumericDomain m_inputDomain = NumericDomain::NormalizedLinear;
    NumericDomain m_outputDomain = NumericDomain::NormalizedLinear;
    ComputePrecision m_precision = ComputePrecision::Float32;
    std::optional<Float64Justification> m_justification;
    OutputPrecisionContract m_outputContract = OutputPrecisionContract::NormalizedOrLossy;
    bool m_outputDomainExplicit = false;
    bool m_contractionAllowed = false;
};

/// How many entries a lookup table over one source representation must hold.
///
/// Only the unsigned eight- and sixteen-bit representations have one. They are the ones whose every
/// value can be enumerated and whose value is already its own index; a signed representation would
/// need a bias convention to turn a code value into an index, and this engine has not stated one, so
/// it answers zero rather than inventing one. Everything else answers zero because its values cannot
/// be enumerated at all, which is what makes a lookup step over any of them refusable by one size
/// check rather than by a list of special cases.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param sampleType the primary source's representation.
/// @return the required entry count, or zero when the representation cannot index a table.
[[nodiscard]] constexpr std::size_t pointwiseLutSizeFor(SampleType sampleType) noexcept
{
    switch (sampleType)
    {
    case SampleType::UInt8:
        return 256u;
    case SampleType::UInt16:
        return 65536u;
    default:
        return 0u;
    }
}

} // namespace an

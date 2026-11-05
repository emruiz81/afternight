// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A validated chain of element-wise steps, planned once and executed many
/// times.
///
/// This is the one substantial mechanism this engine designs rather than adopts. A fused executor
/// that does not beat running one optimized kernel per step is complexity rather than architecture,
/// so the question was answered on measured chains rather than assumed: a chain of cheap steps, whose
/// cost is reading and writing the frame, runs several times faster fused than one pass per step, so
/// the program and its fused executor are kept. What that comparison requires of this type is
/// concrete: every step must also be executable on its own, so that the two ways of running a chain
/// differ at execution time rather than by a rewrite. That is why the vocabulary in `PointwiseOp.h`
/// is a list of steps rather than an expression tree - a tree would be cheaper to fuse and impossible
/// to execute one node at a time against the same inputs.
///
/// A program is immutable once built. It carries what it needs to be checked completely - how many
/// channels a pixel has, how many source planes will be bound, what the source samples are, what
/// width the arithmetic runs at - so that an executor is left with arithmetic and no decisions. It
/// carries no pointers, extents or strides: those are invocation data, and a program that embedded
/// them could only be used once.
///
/// The tables travel inside the program rather than beside it because a lookup table produced by
/// collapsing a chain is part of the meaning of that chain. A table that could be swapped
/// independently would make two programs that compare equal compute different results.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

class PointwiseProgramBuilder;

/// The most steps one program will carry.
///
/// A limit rather than a guess: validation is linear in the step count and every executor runs
/// every step on every pixel, so an unbounded chain would turn a planning cost into a per-pixel one.
/// A chain longer than this is a sequence of programs, which is a thing the caller can express.
inline constexpr std::uint32_t kMaxPointwiseSteps = 64u;

/// The most source planes one program will bind.
///
/// Plane zero is always the primary input, so this is one input plus the masks and blend operands a
/// chain reads beside it.
inline constexpr std::uint32_t kMaxPointwiseSources = 8u;

/// The most channels one pixel of a pointwise program may carry.
///
/// Smaller than the engine's image channel limit on purpose. A channel matrix is the square of this
/// number, and the executor holds a whole pixel in fixed-size working storage; a program over
/// thousands of channels would be a reduction wearing a pointwise chain's clothes.
inline constexpr std::uint32_t kMaxPointwiseChannels = 16u;

/// A chain of element-wise steps that has been checked and may now only be executed.
///
/// Produced only by `PointwiseProgramBuilder`, which the canonicalizer and `fusePointwisePrograms`
/// also build through, so there is no way to reach a program that has not been validated. Copyable
/// and movable, comparable, and free of any reference to the images it will run on.
///
/// Thread-safe to read from any number of threads at once, because it cannot be changed once built.
class PointwiseProgram
{
public:
    /// Copies a program, including its tables.
    PointwiseProgram(const PointwiseProgram&) = default;

    /// Takes over a program's steps and tables.
    PointwiseProgram(PointwiseProgram&&) noexcept = default;

    /// Copies a program over this one.
    ///
    /// @return this program.
    PointwiseProgram& operator=(const PointwiseProgram&) = default;

    /// Takes over a program's steps and tables, leaving the other with none.
    ///
    /// @return this program.
    PointwiseProgram& operator=(PointwiseProgram&&) noexcept = default;

    /// Releases the steps and tables.
    ~PointwiseProgram() = default;

    /// The steps, in execution order.
    ///
    /// Borrowed for as long as this program lives. Never empty: an empty chain is refused when a
    /// program is built.
    ///
    /// @return the steps.
    [[nodiscard]] std::span<const PointwiseOp> steps() const noexcept
    {
        return std::span<const PointwiseOp>(m_steps);
    }

    /// How many channels one pixel carries.
    ///
    /// @return the channel count, between one and `kMaxPointwiseChannels`.
    [[nodiscard]] std::uint32_t channelCount() const noexcept
    {
        return m_channelCount;
    }

    /// How many source planes an execution must bind, including the primary input.
    ///
    /// @return the source count, between one and `kMaxPointwiseSources`.
    [[nodiscard]] std::uint32_t sourceCount() const noexcept
    {
        return m_sourceCount;
    }

    /// What the primary source's samples are.
    ///
    /// Carried because a lookup step's index is the source sample itself, so whether the chain can
    /// hold one at all is a fact about the input rather than about the step. The executor requires
    /// every source and the destination to hold this representation too.
    ///
    /// @return the representation.
    [[nodiscard]] SampleType sourceSampleType() const noexcept
    {
        return m_sourceSampleType;
    }

    /// What the bound sources' values mean.
    ///
    /// Every source shares this declaration, so binary operations combine like domains and never
    /// normalize by inspecting a C++ type.
    ///
    /// @return the validated input numeric domain.
    [[nodiscard]] NumericDomain inputDomain() const noexcept
    {
        return m_inputDomain;
    }

    /// What the values written by the chain mean.
    ///
    /// Together with `inputDomain`, this is the metadata transition a caller applies after a
    /// successful execution. The executor writes samples and does not mutate metadata itself.
    ///
    /// @return the validated output numeric domain.
    [[nodiscard]] NumericDomain outputDomain() const noexcept
    {
        return m_outputDomain;
    }

    /// The width the chain's arithmetic runs at.
    ///
    /// Part of the program's meaning rather than an executor's choice: a chain evaluated in Float32
    /// and the same chain evaluated in Float64 are two different programs that happen to share
    /// their steps, and a table built for one is wrong for the other.
    ///
    /// @return the declared precision.
    [[nodiscard]] ComputePrecision precision() const noexcept
    {
        return m_precision;
    }

    /// Why Float64 arithmetic is required, when it is required.
    ///
    /// This is part of the program's validated numeric contract and is preserved by
    /// canonicalization. Float32 programs carry no justification.
    ///
    /// @return the declared Float64 reason, or no value for a Float32 program.
    [[nodiscard]] std::optional<Float64Justification> precisionJustification() const noexcept
    {
        return m_precisionJustification;
    }

    /// What the chain promises about the values it writes.
    ///
    /// @return the declared output contract.
    [[nodiscard]] OutputPrecisionContract outputContract() const noexcept
    {
        return m_outputContract;
    }

    /// Whether a rewrite may replace two roundings with one.
    ///
    /// False unless a caller asked for it by name. The engine's canonicalization is otherwise
    /// bit-preserving, because a rewrite that is two units in the last place in Float32 has been
    /// measured to become a visible difference in a sixteen-bit output, and a rule that is only
    /// safe until the result is quantized is not a rule.
    ///
    /// @return true when contraction was permitted.
    [[nodiscard]] bool contractionAllowed() const noexcept
    {
        return m_contractionAllowed;
    }

    /// How many tables the program carries.
    ///
    /// @return the table count.
    [[nodiscard]] std::uint32_t tableCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_tables.size());
    }

    /// One table's entries.
    ///
    /// Borrowed for as long as this program lives.
    ///
    /// @param index which table, below `tableCount()`.
    /// @return the entries, or an empty span when the index names no table.
    [[nodiscard]] std::span<const double> table(std::uint32_t index) const noexcept
    {
        if (index >= m_tables.size())
        {
            return {};
        }
        return std::span<const double>(m_tables[index]);
    }

private:
    friend class PointwiseProgramBuilder;

    PointwiseProgram() = default;

    std::vector<PointwiseOp> m_steps;
    std::vector<std::vector<double>> m_tables;
    std::uint32_t m_channelCount = 1u;
    std::uint32_t m_sourceCount = 1u;
    SampleType m_sourceSampleType = SampleType::Float32;
    NumericDomain m_inputDomain = NumericDomain::NormalizedLinear;
    NumericDomain m_outputDomain = NumericDomain::NormalizedLinear;
    ComputePrecision m_precision = ComputePrecision::Float32;
    std::optional<Float64Justification> m_precisionJustification;
    OutputPrecisionContract m_outputContract = OutputPrecisionContract::NormalizedOrLossy;
    bool m_contractionAllowed = false;
};

/// Whether two programs are the same program.
///
/// Compares the steps, the tables and every declaration, so two programs are equal exactly when
/// they compute the same thing from the same inputs at the same width. This is what a
/// canonicalization test asserts against, so it may not be loosened to ignore anything.
///
/// Thread-safe and allocation-free.
///
/// @param left one program.
/// @param right the other.
/// @return true when they are the same program.
[[nodiscard]] AFTERNIGHTENGINE_API bool operator==(const PointwiseProgram& left,
                                                   const PointwiseProgram& right) noexcept;

} // namespace an

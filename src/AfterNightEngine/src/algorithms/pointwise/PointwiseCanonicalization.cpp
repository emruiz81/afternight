// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the rewrites a pointwise chain is allowed, and no others.
//
// Three rules survive the bit-preserving standard, and the third is worth the other two several
// times over.
//
// A scale by exactly one is dropped. For every finite value, every infinity and both zeros,
// multiplying by one returns the same bits.
//
// Adjacent clamps become their intersection. Clamping to `[a, b]` and then to `[c, d]` is clamping
// to `[max(a, c), min(b, d)]` whenever that interval admits a value, by cases on where the input
// falls; when it admits none the composition is a constant rather than a clamp, and the pair is
// left alone rather than rewritten into a clamp whose bounds cross.
//
// An eligible chain becomes one lookup table. This is the rule that pays, and it is exact for a
// reason the other two are not: the table is not derived from the chain by algebra, it is *built by
// running the chain*, at the program's own declared width, at every value the source can hold. The
// rewritten program therefore cannot disagree with the original anywhere, because everywhere is
// where it was measured - except that the chain is run without contraction, which
// `canonicalizePointwiseProgram`'s documentation states. It is available only when the source is an
// enumerable unsigned integer and every step is a function of the value alone - a channel matrix
// reads the other channels, so it is not determined by an index, and the steps that read a source
// plane are excluded as well, although with one plane bound the only one they could read is the
// input itself.
//
// What is deliberately absent is listed in `PointwiseCanonicalization.h`, and the tests assert the
// absence.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseEvaluator.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace an
{
namespace
{

/// The chain with the two local rules applied until neither can fire again.
[[nodiscard]] std::vector<PointwiseOp> rewriteSteps(std::span<const PointwiseOp> steps)
{
    std::vector<PointwiseOp> rewritten;
    rewritten.reserve(steps.size());

    for (const PointwiseOp& op : steps)
    {
        if (op.kind == PointwiseOpKind::Scale && op.factor == 1.0)
        {
            continue;
        }
        if (op.kind == PointwiseOpKind::Clamp && !rewritten.empty() && rewritten.back().kind == PointwiseOpKind::Clamp)
        {
            const double low = (std::max)(rewritten.back().low, op.low);
            const double high = (std::min)(rewritten.back().high, op.high);
            if (low <= high)
            {
                rewritten.back() = pointwiseClamp(low, high);
                continue;
            }
        }
        rewritten.push_back(op);
    }

    if (rewritten.empty())
    {
        // Every step was a scale by one. The chain is the identity, and an identity chain is a
        // thing a caller can write; an empty one is not, so the last of them stands for all.
        rewritten.push_back(pointwiseScale(1.0));
    }
    return rewritten;
}

/// A single-channel program over the same steps, used to build a table by running the chain.
///
/// It keeps the source representation, both domains, the output contract, the width and every
/// table and the contraction permission, so each entry is what the chain computes for one channel.
[[nodiscard]] Result<PointwiseProgram> asSingleChannel(const PointwiseProgram& program,
                                                       std::span<const PointwiseOp> steps)
{
    PointwiseProgramBuilder builder;
    builder.channels(1u)
        .sources(1u)
        .readingSamples(program.sourceSampleType())
        .readingDomain(program.inputDomain())
        .writingDomain(program.outputDomain())
        .promising(program.outputContract());
    if (program.precision() == ComputePrecision::Float64)
    {
        builder.computingIn(ComputePrecision::Float64, program.precisionJustification());
    }
    if (program.contractionAllowed())
    {
        builder.allowingContraction();
    }
    for (std::uint32_t table = 0u; table < program.tableCount(); ++table)
    {
        const std::uint32_t added = builder.addTable(program.table(table));
        static_cast<void>(added);
    }
    for (const PointwiseOp& op : steps)
    {
        builder.append(op);
    }
    return builder.build();
}

} // namespace

Result<PointwiseProgram> fusePointwisePrograms(std::span<const PointwiseProgram> programs)
{
    constexpr const char* kOperation = "an::fusePointwisePrograms";
    const auto failure = [](ErrorCode code, const char* message) {
        return Status::failure(
            code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
    };
    if (programs.size() < 2u)
    {
        return failure(ErrorCode::InvalidArgument, "pointwise fusion needs at least two programs");
    }

    const PointwiseProgram& first = programs.front();
    for (std::size_t index = 1u; index < programs.size(); ++index)
    {
        const PointwiseProgram& previous = programs[index - 1u];
        const PointwiseProgram& current = programs[index];
        if (current.sourceSampleType() != first.sourceSampleType() || current.channelCount() != first.channelCount() ||
            current.precision() != first.precision() ||
            current.precisionJustification() != first.precisionJustification() ||
            current.outputContract() != first.outputContract() ||
            current.contractionAllowed() != first.contractionAllowed() ||
            previous.outputDomain() != current.inputDomain())
        {
            return failure(ErrorCode::IncompatibleArguments,
                           "pointwise stages disagree on shape, arithmetic policy or adjacent numeric domains");
        }
    }

    std::uint32_t sourceCount = first.sourceCount();
    for (const PointwiseProgram& program : programs)
    {
        sourceCount = (std::max)(sourceCount, program.sourceCount());
    }
    PointwiseProgramBuilder builder;
    builder.channels(first.channelCount())
        .sources(sourceCount)
        .readingSamples(first.sourceSampleType())
        .readingDomain(first.inputDomain())
        .writingDomain(programs.back().outputDomain())
        .promising(first.outputContract())
        .computingIn(first.precision(), first.precisionJustification());
    if (first.contractionAllowed())
    {
        builder.allowingContraction();
    }

    std::uint32_t nextTable = 0u;
    for (const PointwiseProgram& program : programs)
    {
        const std::uint32_t tableBase = nextTable;
        for (std::uint32_t table = 0u; table < program.tableCount(); ++table)
        {
            const std::uint32_t added = builder.addTable(program.table(table));
            static_cast<void>(added);
            ++nextTable;
        }
        for (PointwiseOp op : program.steps())
        {
            if (op.kind == PointwiseOpKind::Lut || op.kind == PointwiseOpKind::ChannelMatrix ||
                op.kind == PointwiseOpKind::SampledCurve)
            {
                op.table += tableBase;
            }
            builder.append(op);
        }
    }
    return builder.build();
}

bool pointwiseProgramCollapsesToLut(const PointwiseProgram& program) noexcept
{
    if (program.sourceCount() != 1u || pointwiseLutSizeFor(program.sourceSampleType()) == 0u)
    {
        return false;
    }
    for (const PointwiseOp& op : program.steps())
    {
        if (op.kind == PointwiseOpKind::ChannelMatrix || op.kind == PointwiseOpKind::Mask ||
            op.kind == PointwiseOpKind::Blend || op.kind == PointwiseOpKind::Add ||
            op.kind == PointwiseOpKind::Subtract || op.kind == PointwiseOpKind::Multiply ||
            op.kind == PointwiseOpKind::Divide || op.kind == PointwiseOpKind::Minimum ||
            op.kind == PointwiseOpKind::Maximum || op.kind == PointwiseOpKind::Compare)
        {
            return false;
        }
    }
    return true;
}

Result<PointwiseProgram> canonicalizePointwiseProgram(const PointwiseProgram& program)
{
    const std::vector<PointwiseOp> rewritten = rewriteSteps(program.steps());

    PointwiseProgramBuilder builder;
    builder.channels(program.channelCount())
        .sources(program.sourceCount())
        .readingSamples(program.sourceSampleType())
        .readingDomain(program.inputDomain())
        .writingDomain(program.outputDomain())
        .promising(program.outputContract());
    if (program.precision() == ComputePrecision::Float64)
    {
        builder.computingIn(ComputePrecision::Float64, program.precisionJustification());
    }
    if (program.contractionAllowed())
    {
        builder.allowingContraction();
    }

    if (pointwiseProgramCollapsesToLut(program))
    {
        const Result<PointwiseProgram> perChannel = asSingleChannel(program, rewritten);
        if (!perChannel)
        {
            return perChannel.status();
        }

        const std::size_t entryCount = pointwiseLutSizeFor(program.sourceSampleType());
        std::vector<double> entries(entryCount, 0.0);
        for (std::size_t index = 0u; index < entryCount; ++index)
        {
            const std::array<double, 1u> input{static_cast<double>(index)};
            std::array<double, 1u> result{};
            const Status evaluated = evaluatePointwisePixel(*perChannel, input, result);
            if (!evaluated.ok())
            {
                return evaluated;
            }
            entries[index] = result[0];
        }

        const std::uint32_t table = builder.addTable(entries);
        builder.append(pointwiseLut(table));
    }
    else
    {
        // Carry over only the tables the surviving steps still read, and renumber them, so that two
        // chains that compute the same thing also hold the same tables and therefore compare equal.
        std::vector<PointwiseOp> renumbered;
        renumbered.reserve(rewritten.size());
        std::vector<std::uint32_t> mapping(program.tableCount(), 0u);
        std::vector<bool> mapped(program.tableCount(), false);
        for (const PointwiseOp& op : rewritten)
        {
            PointwiseOp kept = op;
            if (op.kind == PointwiseOpKind::Lut || op.kind == PointwiseOpKind::ChannelMatrix ||
                op.kind == PointwiseOpKind::SampledCurve)
            {
                if (!mapped[op.table])
                {
                    mapping[op.table] = builder.addTable(program.table(op.table));
                    mapped[op.table] = true;
                }
                kept.table = mapping[op.table];
            }
            renumbered.push_back(kept);
        }
        for (const PointwiseOp& op : renumbered)
        {
            builder.append(op);
        }
    }

    Result<PointwiseProgram> canonical = builder.build();
    if (!canonical)
    {
        return Status::failure(ErrorCode::Internal,
                               StatusDetail{.message = "a canonicalization rule produced a chain this engine will "
                                                       "not accept, which is a defect in the rule",
                                            .operation = "canonicalizePointwiseProgram",
                                            .domain = ErrorDomain::Algorithms},
                               canonical.status());
    }
    return canonical;
}

} // namespace an

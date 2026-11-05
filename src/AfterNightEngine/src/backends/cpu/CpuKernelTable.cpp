// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar kernel table, and the selection of one table per operation.
//
// One table per instruction set this build contains, and one place that says which sets those are.
//
// The tables are constants with static storage duration, so they exist before `main` and nothing
// initialises them lazily. That is the other half of "forced selection cannot race with dispatch
// initialisation": the feature detection is a function-local static the standard initialises exactly
// once, and the tables are constants, so between them there is no mutable state for a race to be
// about. The scalar table is defined here; the vector ones are defined in the translation unit that
// compiles their kernels, because that is the only place their entries are constant expressions.
//
// **Which sets this build contains is read from the build rather than written down.** The scalar
// table is the one every build has. Everything above it comes from
// `backends/cpu/SimdKernelTables.h`, which answers out of the vector abstraction's own compiled
// target set - and that set is not the same on the two compilers this engine ships on. A Microsoft
// build has the AVX2 rung and no more, because the abstraction marks AVX-512 broken there; a Clang
// build has both. Asserting a set here would make one of those two builds lie.
//
// The kernel entries differ between the tables, and that difference is the whole point of having
// more than one. The scalar table points at the reference implementations, which are the oracle; the
// vector tables point at kernels compiled for their own instruction set. An operation forced to
// `ComputeBackend::Reference` therefore runs genuinely different code from one left automatic, which
// is what makes a differential comparison a comparison rather than a tautology.
//
// Which table an operation dispatches to is decided here rather than beside the reporting, so that
// what runs and what is reported cannot be derived two different ways; every per-family lookup below
// that answers with a compiled kernel reads it from the table `cpuKernelTableFor` chose.

#include "backends/cpu/CpuKernelTable.h"

#include "backends/cpu/CalibrationCpu.h"
#include "backends/cpu/ColorAdjustmentCpu.h"
#include "backends/cpu/LocalNormalizationCpu.h"
#include "backends/cpu/NormalizationCpu.h"
#include "backends/cpu/PsfFitCpu.h"
#include "backends/cpu/SimdKernelTables.h"
#include "backends/cpu/StackAccumulatorCpu.h"
#include "backends/cpu/StackMethodCpu.h"
#include "backends/cpu/StackRejectionCpu.h"
#include "backends/cpu/StackSourceCpu.h"
#include "backends/cpu/PresentationCpu.h"
#include "backends/cpu/StretchCpu.h"
#include "backends/cpu/WarpCpu.h"
#include "kernels/reference/ColorAdjustmentKernels.h"
#include "kernels/reference/ConversionKernels.h"
#include "kernels/reference/CurveTableKernels.h"
#include "kernels/reference/LayoutKernels.h"
#include "kernels/reference/OrderCountKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <cstddef>

namespace an
{
namespace
{

/// What the scalar table's code was compiled for.
///
/// @return `InstructionSet::Scalar`.
[[nodiscard]] InstructionSet reportScalar() noexcept
{
    return InstructionSet::Scalar;
}

/// The table every build has, and the oracle a conformance run compares against.
constexpr CpuKernelTable kScalarTable{.instructionSet = InstructionSet::Scalar,
                                      .reportInstructionSet = &reportScalar,
                                      .convertRun = &referenceConvertRun,
                                      .strideCopyRun = &referenceStrideCopyRun,
                                      .byteSwapRun = &referenceByteSwapRun,
                                      .copyRun = &referenceCopyRun,
                                      .fillRun = &referenceFillRun,
                                      .deinterleaveRun = &referenceDeinterleaveRun,
                                      .interleaveRun = &referenceInterleaveRun,
                                      .pointwiseFloat32Run = nullptr,
                                      .pointwiseFloat64Run = nullptr,
                                      .pointwiseIntegerRun = nullptr,
                                      .normalizedLowerCurveFloat32Run = nullptr,
                                      .rangeSelectionFloat32Run = nullptr,
                                      .reductionFirstRun = nullptr,
                                      .reductionCenteredRun = nullptr,
                                      .stencilRowRun = nullptr,
                                      .stencilColumnRun = nullptr,
                                      .symmetricGaussianHorizontalRun = nullptr,
                                      .symmetricGaussianVerticalRun = nullptr,
                                      .edgeTightenRowRun = nullptr,
                                      .previewGammaTableRun = nullptr,
                                      .sampledCurveFloat32Run = nullptr,
                                      .curveTableEntriesRun = nullptr,
                                      .curveTableProbesRun = nullptr,
                                      .orderCountFloat32Run = nullptr,
                                      .orderExtremesFloat32Run = nullptr,
                                      .orderGatherFloat32Run = nullptr,
                                      .orderCountBracketSetFloat32Run = nullptr,
                                      .orderGatherIntervalSetFloat32Run = nullptr,
                                      .expFloat32Run = nullptr,
                                      .logFloat32Run = nullptr,
                                      .powFloat32Run = nullptr,
                                      .bilateralRowRun = nullptr,
                                      .chromaticCeilingRun = nullptr,
                                      .maskIntersectRun = nullptr,
                                      .maskUnionRun = nullptr,
                                      .maskInvertRun = nullptr,
                                      .maskPopcountRun = nullptr};

/// The best instruction set an unconstrained CPU operation should use.
///
/// The highest rung of the ladder that this host can run and this build contains, walking down from
/// what the host reported. Scalar is the floor and every build has it, so this always answers.
///
/// @return the set, never `InstructionSet::Automatic`.
[[nodiscard]] InstructionSet bestAvailableInstructionSet() noexcept
{
    const CpuCapabilities capabilities = cpuCapabilities();
    for (std::size_t rung = static_cast<std::size_t>(capabilities.highestSupported); rung > 0u; --rung)
    {
        const InstructionSet candidate = static_cast<InstructionSet>(rung);
        if (supportsInstructionSet(capabilities, candidate) && cpuKernelTableExists(candidate))
        {
            return candidate;
        }
    }
    return InstructionSet::Scalar;
}

/// Whether the compiled run over integer samples evaluates every step of a program as the evaluator does, bit for bit.
///
/// The steps it takes are those whose lane arithmetic is the evaluator's own expression in the same order at the same
/// width - scales, biases, clamps, masks, blends, the four arithmetic steps and comparisons - with contraction off, so
/// no product and sum is fused on one side alone. A minimum or a maximum is left out because a lane's minimum of two
/// zeros of opposite signs is not the platform's, a power and a curve because their lanes approximate, and a table or
/// a matrix because no lane body reads one.
[[nodiscard]] bool integerLanesEvaluate(const PointwiseProgram& program) noexcept
{
    if (program.contractionAllowed())
    {
        return false;
    }
    for (const PointwiseOp& op : program.steps())
    {
        switch (op.kind)
        {
        case PointwiseOpKind::Scale:
        case PointwiseOpKind::Bias:
        case PointwiseOpKind::Clamp:
        case PointwiseOpKind::Mask:
        case PointwiseOpKind::Blend:
        case PointwiseOpKind::Add:
        case PointwiseOpKind::Subtract:
        case PointwiseOpKind::Multiply:
        case PointwiseOpKind::Divide:
        case PointwiseOpKind::Compare:
            break;
        default:
            return false;
        }
    }
    return true;
}

} // namespace

bool cpuKernelTableExists(InstructionSet instructionSet) noexcept
{
    // Scalar is the reference implementation and is compiled into every build unconditionally.
    // Every other rung exists exactly when the vector translation unit compiled kernels for it,
    // which is a property of this binary rather than of this processor.
    return instructionSet == InstructionSet::Scalar || simdCpuKernelTable(instructionSet) != nullptr;
}

const CpuKernelTable& cpuKernelTableFor(const ExecutionContext& execution) noexcept
{
    if (!execution.valid() || execution.backend() == ComputeBackend::Reference)
    {
        return cpuKernelTable(InstructionSet::Scalar);
    }
    const InstructionSet constrained = execution.instructionSet();
    return cpuKernelTable(constrained != InstructionSet::Automatic ? constrained : bestAvailableInstructionSet());
}

PointwiseRunFn pointwiseRunFor(const ExecutionContext& execution, const PointwiseProgram& program) noexcept
{
    // The compiled run lowers only the steps it has a lane body for: no table lookup or channel
    // matrix, a gamma only in Float32, and a sampled curve that interpolates or reads the nearest
    // entry and clamps or extrapolates outside its domain - one that refuses a value there has to
    // fail the run, and the entry below the position is the real-time preview's own executor. A
    // program holding any other step gets null, and the evaluator runs it itself.
    for (const PointwiseOp& op : program.steps())
    {
        const bool compiledCurve =
            op.kind == PointwiseOpKind::SampledCurve &&
            (op.sampling == PointwiseCurveSampling::Linear || op.sampling == PointwiseCurveSampling::Nearest) &&
            op.domainPolicy != PointwiseCurveDomainPolicy::Reject;
        if ((op.kind == PointwiseOpKind::Gamma && program.precision() != ComputePrecision::Float32) ||
            op.kind == PointwiseOpKind::Lut || op.kind == PointwiseOpKind::ChannelMatrix ||
            (op.kind == PointwiseOpKind::SampledCurve && !compiledCurve) || op.kind == PointwiseOpKind::Unknown)
        {
            return nullptr;
        }
    }

    const CpuKernelTable& table = cpuKernelTableFor(execution);
    if (program.sourceSampleType() == SampleType::Float32 && program.precision() == ComputePrecision::Float32)
    {
        return table.pointwiseFloat32Run;
    }
    if (program.sourceSampleType() == SampleType::Float64 && program.precision() == ComputePrecision::Float64)
    {
        return table.pointwiseFloat64Run;
    }
    // An integer image's program - a blend through a mask, a modulation - runs compiled where every step is one whose
    // lanes write the evaluator's bits; any other is evaluated a pixel at a time.
    if (!isFloatingSampleType(program.sourceSampleType()) && table.pointwiseIntegerRun != nullptr &&
        integerLanesEvaluate(program))
    {
        return table.pointwiseIntegerRun(program.sourceSampleType(), program.precision());
    }
    return nullptr;
}

SampledCurveFloat32RunFn sampledCurveFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).sampledCurveFloat32Run;
}

ConvertRunFn convertRunFor(const ExecutionContext& execution, SampleType source, SampleType destination) noexcept
{
    return cpuKernelTableFor(execution).convertRun(source, destination);
}

NormalizedLowerCurveFloat32RunFn normalizedLowerCurveFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).normalizedLowerCurveFloat32Run;
}

RangeSelectionFloat32RunFn rangeSelectionFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).rangeSelectionFloat32Run;
}

CalibrationFloat32RunFn calibrationFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const CalibrationCpuRuns* const runs = simdCalibrationRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float32;
}

CalibrationFloat64RunFn calibrationFloat64RunFor(const ExecutionContext& execution) noexcept
{
    const CalibrationCpuRuns* const runs = simdCalibrationRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float64;
}

WarpCubicRowRunFn warpCubicRowFor(const ExecutionContext& execution) noexcept
{
    const WarpCpuRuns* const runs = simdWarpRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->cubicFloat32;
}

WarpLanczos4RowRunFn warpLanczos4RowFor(const ExecutionContext& execution) noexcept
{
    const WarpCpuRuns* const runs = simdWarpRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->lanczos4Float32;
}

NormalizationFloat32RunFn normalizationFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const NormalizationCpuRuns* const runs = simdNormalizationRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float32;
}

NormalizationFloat64RunFn normalizationFloat64RunFor(const ExecutionContext& execution) noexcept
{
    const NormalizationCpuRuns* const runs = simdNormalizationRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float64;
}

AsinhFloat32RunFn asinhFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const StretchCpuRuns* const runs = simdStretchRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->asinhFloat32;
}

AsinhFloat32RunFn asinhInverseFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const StretchCpuRuns* const runs = simdStretchRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->asinhInverseFloat32;
}

GeneralizedHyperbolicFloat32RunFn generalizedHyperbolicFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const StretchCpuRuns* const runs = simdStretchRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->generalizedHyperbolicFloat32;
}

GeneralizedHyperbolicFloat32RunFn generalizedHyperbolicInverseFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const StretchCpuRuns* const runs = simdStretchRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->generalizedHyperbolicInverseFloat32;
}

MidtonesTransferFloat32RunFn midtonesTransferFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const StretchCpuRuns* const runs = simdStretchRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->midtonesTransferFloat32;
}

LchAdjustFloat32RunFn lchAdjustFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const ColorAdjustmentCpuRuns* const runs = simdColorAdjustmentRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->lchAdjustFloat32;
}

PresentationCodeRowRunFn presentationCodeRowRunFor(const ExecutionContext& execution, SampleType sampleType) noexcept
{
    const PresentationCpuRuns* const runs = simdPresentationRuns(cpuKernelTableFor(execution).instructionSet);
    if (runs == nullptr)
    {
        return nullptr;
    }
    switch (sampleType)
    {
    case SampleType::UInt8:
        return runs->uint8;
    case SampleType::UInt16:
        return runs->uint16;
    default:
        return nullptr;
    }
}

LocalNormalizationFloat32RunFn localNormalizationFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const LocalNormalizationCpuRuns* const runs =
        simdLocalNormalizationRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float32;
}

LocalNormalizationFloat64RunFn localNormalizationFloat64RunFor(const ExecutionContext& execution) noexcept
{
    const LocalNormalizationCpuRuns* const runs =
        simdLocalNormalizationRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float64;
}

// The two point-spread-function evaluations are looked out of the unit that compiles them rather
// than carried as table entries, because a table entry costs the shared vector translation unit a
// thunk per rung and this family's whole reason for living in its own unit is that its object size
// should be attributable to it. The table's own identity is what selects, which is the same
// selection every other family makes; only the indirection is different.
PsfResidualRunFn psfResidualRunFor(const ExecutionContext& execution) noexcept
{
    return simdPsfResidualRun(cpuKernelTableFor(execution).instructionSet);
}

PsfNormalEquationsRunFn psfNormalEquationsRunFor(const ExecutionContext& execution) noexcept
{
    return simdPsfNormalEquationsRun(cpuKernelTableFor(execution).instructionSet);
}

StackAccumulateFloat32RunFn stackAccumulateFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const StackAccumulatorCpuRuns* const runs = simdStackAccumulatorRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float32;
}

StackAccumulateFloat64RunFn stackAccumulateFloat64RunFor(const ExecutionContext& execution) noexcept
{
    const StackAccumulatorCpuRuns* const runs = simdStackAccumulatorRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float64;
}

const StackAccumulateBandsRuns* stackAccumulateBandsRunsFor(const ExecutionContext& execution) noexcept
{
    const StackAccumulatorCpuRuns* const runs = simdStackAccumulatorRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : &runs->bands;
}

StackColumnFloat32RunFn stackColumnFloat32RunFor(const ExecutionContext& execution) noexcept
{
    const StackMethodCpuRuns* const runs = simdStackMethodRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float32;
}

StackColumnFloat64RunFn stackColumnFloat64RunFor(const ExecutionContext& execution) noexcept
{
    const StackMethodCpuRuns* const runs = simdStackMethodRuns(cpuKernelTableFor(execution).instructionSet);
    return runs == nullptr ? nullptr : runs->float64;
}

StackRejectionFloat32RowFn stackRejectionFloat32RowFor(const ExecutionContext& execution,
                                                       const std::uint32_t frames) noexcept
{
    return simdStackRejectionFloat32Row(cpuKernelTableFor(execution).instructionSet, frames);
}

const StackSourceCpuRuns* stackSourceRunsFor(const ExecutionContext& execution) noexcept
{
    return simdStackSourceRuns(cpuKernelTableFor(execution).instructionSet);
}

ReductionFirstRunFn reductionFirstRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).reductionFirstRun;
}

ReductionCenteredRunFn reductionCenteredRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).reductionCenteredRun;
}

DemosaicRunFn demosaicRunFor(const ExecutionContext& execution, DemosaicMethod method) noexcept
{
    if (!isDemosaicMethod(method))
    {
        return nullptr;
    }
    if (execution.backend() == ComputeBackend::Reference)
    {
        return referenceDemosaicRun(method);
    }

    const CpuKernelTable& table = cpuKernelTableFor(execution);
    if (table.demosaicRun != nullptr)
    {
        DemosaicRunFn compiled = table.demosaicRun(method);
        if (compiled != nullptr)
        {
            return compiled;
        }
    }
    if (execution.requestedBackend() == ComputeBackend::Cpu &&
        execution.backendFallback() == BackendFallback::Forbidden)
    {
        return nullptr;
    }
    return referenceDemosaicRun(method);
}

StencilRowRunFn stencilRowRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).stencilRowRun;
}

StencilColumnRunFn stencilColumnRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).stencilColumnRun;
}

SymmetricGaussianHorizontalRunFn symmetricGaussianHorizontalRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).symmetricGaussianHorizontalRun;
}

SymmetricGaussianVerticalRunFn symmetricGaussianVerticalRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).symmetricGaussianVerticalRun;
}

EdgeTightenRowRunFn edgeTightenRowRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).edgeTightenRowRun;
}

PreviewGammaTableRunFn previewGammaTableRunForHost() noexcept
{
    return cpuKernelTable(bestAvailableInstructionSet()).previewGammaTableRun;
}

OrderCountFloat32RunFn orderCountFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).orderCountFloat32Run;
}

OrderExtremesFloat32RunFn orderExtremesFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).orderExtremesFloat32Run;
}

OrderGatherFloat32RunFn orderGatherFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).orderGatherFloat32Run;
}

OrderCountBracketSetFloat32RunFn orderCountBracketSetFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).orderCountBracketSetFloat32Run;
}

OrderGatherIntervalSetFloat32RunFn orderGatherIntervalSetFloat32RunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).orderGatherIntervalSetFloat32Run;
}

CurveTableRuns curveTableRunsFor(const ExecutionContext& execution) noexcept
{
    const CpuKernelTable& table = cpuKernelTableFor(execution);
    return CurveTableRuns{.entries = table.curveTableEntriesRun, .probes = table.curveTableProbesRun};
}

CurveTableRuns curveTableRunsForHost() noexcept
{
    const CpuKernelTable& table = cpuKernelTable(bestAvailableInstructionSet());
    return CurveTableRuns{.entries = table.curveTableEntriesRun, .probes = table.curveTableProbesRun};
}

MathRunFn mathRunFor(const ExecutionContext& execution, MathFunction function) noexcept
{
    const CpuKernelTable& table = cpuKernelTableFor(execution);
    switch (function)
    {
    case MathFunction::Exp:
        return table.expFloat32Run;
    case MathFunction::Log:
        return table.logFloat32Run;
    case MathFunction::Pow:
        return table.powFloat32Run;
    case MathFunction::Unknown:
        break;
    }
    return nullptr;
}

BilateralRowRunFn bilateralRowRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).bilateralRowRun;
}

ChromaticCeilingRunFn chromaticCeilingRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).chromaticCeilingRun;
}

MaskCombineRunFn maskIntersectRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).maskIntersectRun;
}

MaskCombineRunFn maskUnionRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).maskUnionRun;
}

MaskInvertRunFn maskInvertRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).maskInvertRun;
}

MaskPopcountRunFn maskPopcountRunFor(const ExecutionContext& execution) noexcept
{
    return cpuKernelTableFor(execution).maskPopcountRun;
}

const CpuKernelTable& cpuKernelTable(InstructionSet instructionSet) noexcept
{
    const CpuKernelTable* const compiled = simdCpuKernelTable(instructionSet);
    if (compiled != nullptr)
    {
        return *compiled;
    }
    // The scalar table is the answer for everything this build has no table of its own for. It is
    // always correct - just slower - which is the right way round for a fallback in a function that
    // cannot report a failure.
    return kScalarTable;
}

} // namespace an

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the function table one instruction set's kernels are reached through.
//
// Runtime dispatch in this engine is a table lookup, not a branch inside a kernel. The difference
// is what makes an instruction set forceable: a table is chosen once per operation and every entry
// in it belongs to that set, so an operation asked to run on the reference implementation runs on
// the reference implementation everywhere rather than wherever somebody remembered to check.
//
// The table carries its own identity, and that is the load-bearing part of this file rather than a
// convenience. `instructionSet` is what the selection *believes*; `reportInstructionSet` is what the
// compiled code *says*, and the two are read from different places on purpose. A table whose entries
// were built for the wrong set - the ordinary mistake when a per-instruction-set translation unit is
// added - disagrees with itself, and a test can see that disagreement without a kernel producing a
// wrong pixel first.
//
// Beside the identity, every kernel family arrives here as a function pointer. The scalar table
// points the conversion and layout families at the reference implementations and leaves every other
// family null, so an operation on it runs its own scalar loop; the compiled tables point at the
// optimised kernels. The difference between the two is what a conformance run measures.
//
// Some families are reached through a lookup in the unit that compiles them rather than through an
// entry here - calibration, warp, normalization, local normalization, point-spread fitting and the
// stacking kernels - so that each family's object size stays attributable to its own unit; they are
// selected by this table's `instructionSet` in `CpuKernelTable.cpp`.

#pragma once

#include "kernels/reference/AlgorithmKernels.h"
#include "kernels/reference/ConversionKernels.h"
#include "kernels/reference/CurveTableKernels.h"
#include "kernels/reference/DemosaicKernels.h"
#include "kernels/reference/LayoutKernels.h"
#include "kernels/reference/OrderCountKernels.h"

#include <AfterNightEngine/Runtime/CpuCapabilities.h>

namespace an
{

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// Everything one instruction set's compiled code is reached through.
///
/// A plain aggregate of function pointers: the identity every table carries, then one entry per
/// kernel family. The entries from `pointwiseFloat32Run` on, and `demosaicRun`, are null in the
/// scalar table.
struct CpuKernelTable
{
    /// Which instruction set this table was selected as.
    ///
    /// What the dispatch believes. Compare it against `reportInstructionSet()` to find a table
    /// wired to the wrong implementation.
    InstructionSet instructionSet = InstructionSet::Scalar;

    /// What the compiled code behind this table says it was built for.
    ///
    /// Never null. It is a function rather than a second field because a field would be filled in
    /// by the same line of code that fills in `instructionSet` and would therefore agree with it by
    /// construction; a function is compiled into the translation unit whose flags decide what the
    /// code actually is.
    InstructionSet (*reportInstructionSet)() noexcept = nullptr;

    /// This table's conversion kernel for one ordered pair of representations.
    ///
    /// A function that looks one up rather than sixty-four entries: the pair is two run-time values
    /// at every call site that matters - a codec knows what a file holds only after it has read the
    /// header - so the lookup has to happen somewhere, and doing it once behind the table keeps the
    /// table a fixed size as representations are added.
    ///
    /// Never null.
    ConvertRunFn (*convertRun)(SampleType source, SampleType destination) noexcept = nullptr;

    /// This table's strided-copy kernel for one representation, which is what a repack is made of.
    ///
    /// Never null.
    StrideCopyRunFn (*strideCopyRun)(SampleType sampleType) noexcept = nullptr;

    /// This table's byte-swap kernel for one representation.
    ///
    /// Never null, though what it returns is null for the one-byte representations, which have one
    /// order and nothing to swap.
    ByteSwapRunFn (*byteSwapRun)(SampleType sampleType) noexcept = nullptr;

    /// This table's contiguous-copy kernel for one representation.
    ///
    /// Never null. The four families below move samples without reading what they mean, so what
    /// their lookups actually resolve is an element width; the representation is what every caller
    /// holds, so it is what the lookup takes.
    CopyRunFn (*copyRun)(SampleType sampleType) noexcept = nullptr;

    /// This table's run-fill kernel for one representation.
    ///
    /// Never null.
    FillRunFn (*fillRun)(SampleType sampleType) noexcept = nullptr;

    /// This table's channel-splitting kernel for one representation.
    ///
    /// Never null. What it returns handles two to `kMaxInterleavedChannels` channels; a wider pixel
    /// is moved a channel at a time through `strideCopyRun`, and the operation above chooses.
    DeinterleaveRunFn (*deinterleaveRun)(SampleType sampleType) noexcept = nullptr;

    /// This table's channel-joining kernel for one representation.
    ///
    /// Never null, with the same channel-count range as `deinterleaveRun`.
    InterleaveRunFn (*interleaveRun)(SampleType sampleType) noexcept = nullptr;

    /// This table's fused Float32 pointwise run.
    PointwiseRunFn pointwiseFloat32Run = nullptr;

    /// This table's fused Float64 pointwise run.
    PointwiseRunFn pointwiseFloat64Run = nullptr;

    /// This table's fused pointwise run over integer samples, for one representation at one
    /// arithmetic width.
    ///
    /// A function that looks one up rather than an entry per pair, as the conversion's is. Null in the
    /// scalar table.
    PointwiseIntegerRunLookupFn pointwiseIntegerRun = nullptr;

    /// This table's normalized Float32 lower-neighbour lookup run.
    NormalizedLowerCurveFloat32RunFn normalizedLowerCurveFloat32Run = nullptr;

    /// This table's normalized Float32 range-selection run.
    RangeSelectionFloat32RunFn rangeSelectionFloat32Run = nullptr;

    /// This table's first-pass reduction run.
    ReductionFirstRunFn reductionFirstRun = nullptr;

    /// This table's centered second-pass reduction run.
    ReductionCenteredRunFn reductionCenteredRun = nullptr;

    /// This table's separable horizontal stencil run.
    StencilRowRunFn stencilRowRun = nullptr;

    /// This table's separable vertical stencil run.
    StencilColumnRunFn stencilColumnRun = nullptr;

    /// This table's symmetric Float32 horizontal Gaussian row.
    SymmetricGaussianHorizontalRunFn symmetricGaussianHorizontalRun = nullptr;

    /// This table's symmetric Float32 vertical Gaussian row.
    SymmetricGaussianVerticalRunFn symmetricGaussianVerticalRun = nullptr;

    /// This table's interior run of the four-direction edge tightening.
    EdgeTightenRowRunFn edgeTightenRowRun = nullptr;

    /// This table's run of the real-time preview's gamma table.
    PreviewGammaTableRunFn previewGammaTableRun = nullptr;

    /// This table's run of a one-step Float32 sampled curve.
    SampledCurveFloat32RunFn sampledCurveFloat32Run = nullptr;

    /// This table's run of a sampled curve table's entries.
    CurveTableEntriesRunFn curveTableEntriesRun = nullptr;

    /// This table's run of a sampled curve table's distance from its curve.
    CurveTableProbesRunFn curveTableProbesRun = nullptr;

    /// This table's count of an exact selection's bracket over Float32 samples.
    OrderCountFloat32RunFn orderCountFloat32Run = nullptr;

    /// This table's count and extremes of an exact selection's samples over Float32 samples.
    OrderExtremesFloat32RunFn orderExtremesFloat32Run = nullptr;

    /// This table's gather of an exact selection's bin over Float32 samples.
    OrderGatherFloat32RunFn orderGatherFloat32Run = nullptr;

    /// This table's count of an exact selection's set of brackets over Float32 samples.
    OrderCountBracketSetFloat32RunFn orderCountBracketSetFloat32Run = nullptr;

    /// This table's gather of an exact selection's set of intervals over Float32 samples.
    OrderGatherIntervalSetFloat32RunFn orderGatherIntervalSetFloat32Run = nullptr;

    /// This table's compiled natural exponential over Float32 samples.
    ///
    /// Null in the scalar table, which is what makes the reference backend the reference: with no
    /// compiled run to reach, the family evaluates through the platform's math library.
    MathRunFn expFloat32Run = nullptr;

    /// This table's compiled natural logarithm over Float32 samples.
    MathRunFn logFloat32Run = nullptr;

    /// This table's compiled power over Float32 samples, composed from the two above it.
    MathRunFn powFloat32Run = nullptr;

    /// This table's compiled bilateral row run, which evaluates the exponential above it for a
    /// vector of adjacent outputs at a time and keeps their sums in registers. Null in the scalar
    /// table.
    BilateralRowRunFn bilateralRowRun = nullptr;

    /// This table's compiled chromatic ceiling row, storing the scalar row's bits. Null in the
    /// scalar table.
    ChromaticCeilingRunFn chromaticCeilingRun = nullptr;

    /// This table's compiled packed-mask intersection.
    MaskCombineRunFn maskIntersectRun = nullptr;

    /// This table's compiled packed-mask union.
    MaskCombineRunFn maskUnionRun = nullptr;

    /// This table's compiled packed-mask inversion.
    MaskInvertRunFn maskInvertRun = nullptr;

    /// This table's compiled packed-mask population count.
    MaskPopcountRunFn maskPopcountRun = nullptr;

    /// This table's compiled tile estimate for one demosaic method.
    ///
    /// A function that looks one up rather than one entry per method: each method is compiled into
    /// its own translation unit so the per-instruction-set object budget stays attributable, and a
    /// lookup keeps the table one entry wide as methods are added. Null in the scalar table, and
    /// allowed to answer null for a method this build has no vector kernel for. The family then
    /// either reports and runs the scalar reference or refuses an explicit CPU request whose
    /// fallback policy is forbidden.
    DemosaicRunFn (*demosaicRun)(DemosaicMethod method) noexcept = nullptr;
};

/// The table for one instruction set.
///
/// Thread-safe, reentrant, and allocation-free: the tables are immutable constants with static
/// storage duration, so there is no initialisation for a forced selection to race against.
///
/// @param instructionSet which set. `InstructionSet::Automatic`, and any set this build has no
///        table for, gives the scalar table - the one every build has.
/// @return the table, which outlives every caller.
[[nodiscard]] const CpuKernelTable& cpuKernelTable(InstructionSet instructionSet) noexcept;

/// Whether this build has a table for an instruction set.
///
/// @param instructionSet which set.
/// @return true when `cpuKernelTable` would return that set's own table rather than the scalar one.
[[nodiscard]] bool cpuKernelTableExists(InstructionSet instructionSet) noexcept;

/// The table one operation dispatches to.
///
/// The whole of the selection, in one function, so that the answer an operation gets and the answer
/// `Runtime/BackendDispatch.h` reports cannot come from two different derivations. The reference
/// backend takes the scalar table whatever instruction set the host has; a CPU operation takes the
/// set it was constrained to, or the best one this host can run and this build contains.
///
/// Thread-safe with respect to `execution`, which is only read, and safe to call from several
/// threads at once: the tables are immutable constants.
///
/// @param execution the operation. One that holds nothing gets the scalar table, which is always
///        correct.
/// @return the table, which outlives every caller.
[[nodiscard]] const CpuKernelTable& cpuKernelTableFor(const ExecutionContext& execution) noexcept;

} // namespace an

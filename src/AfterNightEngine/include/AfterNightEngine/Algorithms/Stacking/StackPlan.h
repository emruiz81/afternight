// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Reviewable stacking strategy, memory, passes and read costs; no algorithms.
///
/// `planStack` in `Algorithms/Stacking/StackPlanner.h` turns a checked model and a
/// `StackPlanningRequest` - the facts about the run the model does not carry - into a `StackPlan`, a
/// plain value a caller can read before anything is allocated or read. Each part states a decision
/// and why it was made: `StackStrategyPlan` whether a pixel's samples are accumulated one frame at
/// a time or held together, `StackPrecisionPlan` the width each stage runs in, `StackTilePlan` how
/// the frame is cut into full-width bands of rows, `StackPassPlan` the passes in order,
/// `StackMemoryPlan` every buffer by name against the budget, `StackIoPlan` how often each frame is
/// read, and `StackBackendPlan` which implementation runs. `StackWorkingSet` takes exactly the
/// memory the plan names, and `executeStack` follows its passes.
///
/// The byte constants are the figures the plan's arithmetic is written in, stated here so a caller
/// can repeat it and the implementation can prove at compile time that its records fit them.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <array>
#include <cstdint>

namespace an
{

/// The representation a tile holds a frame's samples as.
///
/// The width the storage defaults to under the stack's lossy contract: Float64 for Float64
/// storage, Float32 for every other representation.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param input what the frames are stored as.
/// @return `SampleType::Float64` or `SampleType::Float32`.
[[nodiscard]] constexpr SampleType stackTileSampleTypeOf(SampleType input) noexcept
{
    return workingSampleTypeOf(defaultComputePrecision(input, OutputPrecisionContract::NormalizedOrLossy));
}

/// Where the samples of an output pixel are while they are being combined.
enum class StackAccumulationStrategy : std::uint8_t
{
    /// Accumulators the size of the output are updated by one frame at a time. The frame count
    /// never enters the memory, and each frame is read once. Under region access, a stack that
    /// `stackAccumulatorsCoverOneTile` admits keeps them the size of one tile instead, finalizing
    /// each tile's rows before the next tile is read.
    WholeOutputAccumulators,

    /// Every frame's samples for one tile are held at once, and each pixel's column of samples is
    /// combined by a worker. The memory grows with the frame count, which is what the tile bounds.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    TileSampleHistory
};

/// Whether a value names a strategy this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param strategy the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackAccumulationStrategy(StackAccumulationStrategy strategy) noexcept
{
    return static_cast<std::uint8_t>(strategy) <=
           static_cast<std::uint8_t>(StackAccumulationStrategy::TileSampleHistory);
}

/// The strategy's own name.
///
/// Thread-safe and allocation-free.
///
/// @param strategy the strategy to name. A value outside the enumeration is named
///        `"UnknownStackAccumulationStrategy"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
stackAccumulationStrategyName(StackAccumulationStrategy strategy) noexcept;

/// Why a plan accumulates the way it does.
enum class StackStrategyReason : std::uint8_t
{
    /// The combination is a streaming statistic: a mean, a sum or an extremum updates an
    /// accumulator with one sample at a time.
    StreamingCombination,

    /// A rejection judges every sample of a pixel against the others, so all of them are held.
    RejectionNeedsSampleHistory,

    /// A median is a rank over every sample of a pixel, so all of them are held.
    MedianNeedsSampleHistory,

    /// The auto-adaptive weighted mean iterates its weights over every sample of a pixel, so all
    /// of them are held.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AutoAdaptiveNeedsSampleHistory
};

/// The reason's own name.
///
/// Thread-safe and allocation-free.
///
/// @param reason the reason to name. A value outside the enumeration is named
///        `"UnknownStackStrategyReason"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackStrategyReasonName(StackStrategyReason reason) noexcept;

/// Why a plan's tile is the size it is.
enum class StackTileReason : std::uint8_t
{
    /// Nothing bounds the working set - the engine context's budget is the default that refuses
    /// nothing and the caller stated no limit - so the tile is the whole frame.
    WholeFrameUnbounded,

    /// A budget bounds the working set and the whole frame fits inside it.
    WholeFrameFits,

    /// A budget bounds the working set and the tile is the largest band of rows that fits.
    BudgetBound,

    /// A sample-history tile under region access holds one cache-sized row band per admitted
    /// worker. The footprint includes every frame's band and its share of the output; a row
    /// larger than `kStackCacheBandBytes` still gets one row per worker before harder limits apply.
    CacheBand,

    /// Under region access the source decodes pieces that span more rows than the budget band or
    /// the cache band would hold - the subblocks of a compressed file - so the tile holds the rows
    /// one piece spans, `StackPlanningRequest::sourceGranularityRows`, which the budget allows, and
    /// consecutive tiles decode each piece about once.
    SourceGranularity,

    /// The caller's `StackPlanningRequest::maximumTileRows` is smaller than what the budget, the
    /// cache band or the source's granularity under region access would allow, so the cap decided.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CallerRowCap
};

/// The reason's own name.
///
/// Thread-safe and allocation-free.
///
/// @param reason the reason to name. A value outside the enumeration is named
///        `"UnknownStackTileReason"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackTileReasonName(StackTileReason reason) noexcept;

/// Why the tile holds samples at the width it does.
enum class StackPrecisionReason : std::uint8_t
{
    /// The storage is one of the seven representations up to thirty-two bits, whose default width
    /// under the stack's lossy contract is Float32.
    DefaultFloat32,

    /// The storage is Float64, which Float32 would truncate.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    StoredInFloat64
};

/// The reason's own name.
///
/// Thread-safe and allocation-free.
///
/// @param reason the reason to name. A value outside the enumeration is named
///        `"UnknownStackPrecisionReason"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackPrecisionReasonName(StackPrecisionReason reason) noexcept;

/// One of the passes a stack runs, in the order they run.
enum class StackPassKind : std::uint8_t
{
    /// Every frame is read whole to measure what brings it to the reference frame: the statistics
    /// a global normalization matches on, the surface a local one fits, or both.
    MeasureNormalization,

    /// Every frame is read whole to measure the local signal-to-noise grid its weight surface is
    /// built from.
    MeasureLocalWeights,

    /// The frames are read, tile by tile, and combined.
    Combine,

    /// The accumulators are narrowed into the output, the maps and the mask are completed, and the
    /// report's summaries are composed. Reads nothing.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Finalize
};

/// The kind's own name.
///
/// Thread-safe and allocation-free.
///
/// @param kind the kind to name. A value outside the enumeration is named `"UnknownStackPassKind"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackPassKindName(StackPassKind kind) noexcept;

/// The most passes one plan holds: the two measurements, the combine and the finalization.
inline constexpr std::size_t kMaxStackPasses = 4u;

/// The bytes of one sample of every output map: a thirty-two-bit count for a rejection or coverage
/// map and a Float32 value for a variance, gain or weight-sum map.
inline constexpr std::uint64_t kStackMapSampleBytes = 4u;

/// The bytes of one whole-output accumulator sample, which is a double.
inline constexpr std::uint64_t kStackAccumulatorBytes = 8u;

/// The bytes of one whole-output sample count, which is a thirty-two-bit integer because a stack
/// admits fewer than that many frames.
inline constexpr std::uint64_t kStackCountBytes = 4u;

/// The target footprint per admitted worker for a sample-history tile under region access.
///
/// A sample-history stack writes every frame's band of a tile through the source path and then
/// reads them back through the column kernels. Each admitted worker gets a complete-row band
/// targeting this footprint, including every input frame and the corresponding output. One row
/// is the minimum when it already exceeds the target. Giving the whole operation only one band
/// can leave too few rows to use its workers and multiply source and scheduler hand-offs.
/// The memory budget and caller row cap still narrow the combined band: this is a locality target,
/// not a memory limit. A streaming stack under region access gets no such band: it
/// folds every frame's band of a tile into the terms in one pass whose frame loop is inside the
/// row loop, so an output row's terms stay in the private cache across the frames however wide the
/// tile is. The bands themselves do not when the source decodes them into the tile: they are read
/// back once the last frame's band has landed, so a caller whose source does that states
/// `StackPlanningRequest::maximumTileRows` to keep the tile inside the last-level cache. A source
/// that serves regions serves these row bands without a seek of its own.
inline constexpr std::uint64_t kStackCacheBandBytes = 1024u * 1024u;

/// The bytes one local-weight grid node holds per frame: a Float32 level and a Float32 quality
/// while the grids are measured, and the finished weight beside that quality once every frame has
/// been judged, from which the pixel's weight is formed on read.
inline constexpr std::uint64_t kLocalWeightNodeBytes = 8u;

/// The bytes one candidate record of the generalized extreme Studentized deviate test holds: the
/// sample, its index and its side, padded to sixteen.
inline constexpr std::uint64_t kStackGesdtCandidateBytes = 16u;

/// The boundary every per-worker scratch term is rounded up to, so that a worker's column and its
/// working copies each begin on a vector-aligned address.
inline constexpr std::uint64_t kStackScratchAlignmentBytes = kDefaultBufferAlignmentBytes;

/// The most pixel columns a compiled combination kernel judges at once.
///
/// The widest Float64 vector this engine compiles holds eight lanes, and a column kernel that runs
/// one pixel per lane needs that many columns of workspace side by side. The figure is a property
/// of the scratch contract rather than of any one build, so a plan is the same whatever instruction
/// set the host runs; a scalar row uses one column of it.
inline constexpr std::uint32_t kStackSampleColumnLanes = 8u;

/// Per-frame scratch for a median or auto-adaptive worker's borrowed sample and validity record.
/// The implementation proves that the private record fits this reservation and its alignment.
inline constexpr std::uint64_t kStackColumnFrameRecordBytes = 128u;

/// The most cells one axis of a local-normalization grid the source path reads a band through may
/// hold.
///
/// The local surface's grid size is a property of the coefficients a stack applies, which the
/// model does not carry, so the per-worker row scratch the source path needs is sized for the
/// largest grid the normalization domain admits rather than for the one a run happens to use. The
/// implementation proves at compile time that this is that domain's ceiling.
inline constexpr std::uint32_t kStackSourceGridCeiling = 256u;

/// How many arrays of one value per node column one worker mixes for one row of the local
/// surface: the reference, target and scale grids' mixed rows, each with its lower and upper bound.
inline constexpr std::uint64_t kStackSourceRowArrays = 9u;

/// The bytes the frame-state block reserves per frame for its band record and retained mask view.
/// The executor stores the band array first and the mask-view array after it, proving their combined
/// size and alignment at compile time. The borrowed mask bytes outlive the combination of the tile.
inline constexpr std::uint64_t kStackBandRecordBytes = 192u;

/// Per-frame scratch for the rejection driver's checked row description. Preparing this record
/// once per row keeps layout and mask interpretation outside the sample loop.
inline constexpr std::uint64_t kStackRejectionRowRecordBytes = 128u;

/// The bytes the frame-state block reserves for one frame's rejection-rate record under a
/// rejection: the rate and whether the frame was rated, which `Algorithms/Stacking/StackQuality.h`
/// states and the executor proves fits at compile time.
inline constexpr std::uint64_t kStackFrameRateRecordBytes = 16u;

/// The bytes of one entry of every per-frame counter the executor keeps: a sixty-four-bit count.
inline constexpr std::uint64_t kStackFrameCounterBytes = 8u;

/// The bytes the frame-state block reserves for one worker's scratch-slice record: the span the
/// executor hands the kernels for that worker's slice, which the executor proves fits at compile
/// time.
inline constexpr std::uint64_t kStackWorkerSliceRecordBytes = 16u;

/// What a caller states about the run, beyond the model.
///
/// A plain aggregate with a default for every member, so a caller names only what it is changing.
/// Everything here is a fact the descriptor does not carry - how the source reads, what is already
/// prepared and held, and what the caller wants bounded.
struct StackPlanningRequest
{
    /// How the source reads one frame.
    StackSourceAccess access = StackSourceAccess::WholeFrame;

    /// How many frames the source may hold decoded at once. At least one; each multiplies the
    /// read buffer.
    std::uint32_t concurrentFrameReads = 1u;

    /// Whether the normalization coefficients the model names are already known, so no
    /// measurement pass is planned for them. Read only under a scope other than none.
    bool normalizationPrepared = false;

    /// Whether the local weight grids are already known, so no measurement pass is planned for
    /// them. Read only when the model enables a local weighting.
    bool localWeightsPrepared = false;

    /// Bytes the caller already holds for this stack and keeps resident through every pass -
    /// prepared coefficients, prepared grids, weight measurements. Counted as persistent state.
    std::uint64_t preparedStateBytes = 0u;

    /// Residency of a local-normalization document being fitted by the executor, including its
    /// frame records and grids. Counted from the start as a conservative persistent reservation;
    /// the document owns these bytes, so StackWorkingSet does not allocate a second copy.
    std::uint64_t fittedNormalizationBytes = 0u;

    /// Peak temporary memory of a composed local-normalization fit or apply. The executor
    /// computes it from the fit request before reading; the planner also counts global measurement.
    std::uint64_t normalizationScratchBytes = 0u;

    /// Peak temporary memory of optional final diagnostics. The planner additionally counts
    /// the rejection-frame outlier summary required by any rejected stack.
    std::uint64_t diagnosticsScratchBytes = 0u;

    /// The most bytes this stack's working set may reach, or zero for no limit of the caller's
    /// own. Narrows the engine context's budget and never widens it.
    std::uint64_t workingSetLimitBytes = 0u;

    /// The most rows one tile may hold, or zero for no cap. A cap smaller than what the budget
    /// allows decides the tile; the unit of progress and cancellation is the tile, which is what a
    /// caller caps it for.
    std::uint32_t maximumTileRows = 0u;

    /// How many rows the smallest piece the source decodes whole spans, or zero when a band costs
    /// its own rows alone. Read under region access only: a source that decodes a piece again for
    /// every band that reaches it - the subblocks of a compressed file - has a narrower tile widened
    /// to this, or to the most rows the budget allows when that is fewer, past the cache band and
    /// before the caller's cap, so consecutive tiles decode each piece about once.
    /// `ImageRowReader::decodeGranularityRows` answers it for a source that reads files.
    std::uint32_t sourceGranularityRows = 0u;

    /// Whether the output, its mask and its maps are handed to the caller tile by tile as each tile
    /// is finished, so they are held for one tile's rows - counted per row like the tile, from the
    /// first pass to the last - rather than for the whole output. Refused where
    /// `stackOutputCanCoverOneTile` does not hold.
    bool outputByTile = false;

    /// Whether two requests state the same run.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackPlanningRequest& left,
                                                   const StackPlanningRequest& right) noexcept = default;
};

/// Where the samples are while they combine, and why.
struct StackStrategyPlan
{
    /// The strategy.
    StackAccumulationStrategy strategy = StackAccumulationStrategy::WholeOutputAccumulators;

    /// Why.
    StackStrategyReason reason = StackStrategyReason::StreamingCombination;

    /// Whether two plans state the same strategy.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackStrategyPlan& left,
                                                   const StackStrategyPlan& right) noexcept = default;
};

/// Which width each stage of the stack runs in.
struct StackPrecisionPlan
{
    /// What the frames are stored as.
    SampleType inputSampleType = SampleType::Float32;

    /// What the tile holds a frame's samples as: Float64 for Float64 storage and Float32 for every
    /// other representation.
    SampleType tileSampleType = SampleType::Float32;

    /// What the combined image is written as.
    SampleType outputSampleType = SampleType::Float32;

    /// The width the samples of a pixel are combined and accumulated in. Always Float64.
    ComputePrecision accumulation = ComputePrecision::Float64;

    /// Why the accumulation is Float64. Always the stacking justification.
    Float64Justification justification = Float64Justification::StackingAccumulation;

    /// Why the tile holds the width it does.
    StackPrecisionReason reason = StackPrecisionReason::DefaultFloat32;

    /// Whether two plans state the same widths.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackPrecisionPlan& left,
                                                   const StackPrecisionPlan& right) noexcept = default;
};

/// How the frame is cut into tiles.
///
/// A tile is a full-width band of rows: the last band may be shorter, and no tile is narrower
/// than the frame.
struct StackTilePlan
{
    /// Columns in every tile, which is the frame's width.
    std::uint32_t width = 0u;

    /// Rows in every tile but possibly the last, at least one.
    std::uint32_t rows = 0u;

    /// How many tiles cover the frame.
    std::uint32_t tileCount = 0u;

    /// The cap the caller stated, or zero.
    std::uint32_t requestedMaximumRows = 0u;

    /// Why the rows are what they are.
    StackTileReason reason = StackTileReason::WholeFrameUnbounded;

    /// Whether two plans state the same tiling.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackTilePlan& left,
                                                   const StackTilePlan& right) noexcept = default;
};

/// One pass over the input, or the finalization that reads none.
struct StackPass
{
    /// What the pass does.
    StackPassKind kind = StackPassKind::Combine;

    /// How many tiles the pass walks; zero for a pass that reads whole frames.
    std::uint32_t tileCount = 0u;

    /// How many times a frame is opened during the pass, over every frame: a whole-frame read or
    /// a region read counts one each.
    std::uint64_t frameReads = 0u;

    /// How many bytes of input the pass reads, counting a frame every time it is read.
    std::uint64_t inputBytes = 0u;

    /// The most bytes the pass holds at once, counting the persistent state.
    std::uint64_t workingSetBytes = 0u;

    /// Whether two passes are the same.
    ///
    /// @param left one pass.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackPass& left, const StackPass& right) noexcept = default;
};

/// The passes a stack runs, in order.
struct StackPassPlan
{
    /// The passes; only the first `count` are meaningful.
    std::array<StackPass, kMaxStackPasses> passes{};

    /// How many of `passes` the plan holds, at least two: the combine and the finalization.
    std::size_t count = 0u;

    /// Whether two plans hold the same passes.
    ///
    /// Not `noexcept`, because an array's comparison is not.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackPassPlan& left, const StackPassPlan& right) = default;
};

/// Every buffer the stack holds, by name, and what they add up to against the budget.
///
/// The persistent figures live from the first pass to the last; the scratch, tile and read-buffer
/// figures belong to the combine pass; the measurement workspace belongs to the measurement
/// passes. The peak is the largest working set of any pass.
struct StackMemoryPlan
{
    /// The combined image, at the output representation - or one tile's rows of it when
    /// `outputCoversOneTile` says the caller takes the output tile by tile.
    std::uint64_t outputImageBytes = 0u;

    /// The output's explicit validity mask, packed one bit per pixel per channel, over the same
    /// rows as the image.
    std::uint64_t outputMaskBytes = 0u;

    /// Every requested map, at `kStackMapSampleBytes` per output element each, over the same rows as
    /// the image.
    std::uint64_t outputMapBytes = 0u;

    /// The accumulators of a streaming strategy: the running sum or extremum, the sample count,
    /// and the weight sum, weight squares and variance moments when they are asked for. They cover
    /// the whole output, or the tile's rows when `stackAccumulatorsCoverOneTile` says the stack
    /// finalizes each tile as it is combined - `stackAccumulatorBytesPerRow` times the height or
    /// the tile's rows. Zero under sample history, whose combination writes the output directly.
    std::uint64_t accumulatorBytes = 0u;

    /// The per-frame tables and the executor's per-frame bookkeeping: one weight per frame and
    /// channel; one offset and one scale per frame and channel under a global normalization; and
    /// `stackFrameBookkeepingBytes` - the admitted and rejected sample counts per frame, the
    /// coverage histogram, and under a rejection or sample history the records
    /// `stackFrameBookkeepingLayoutOf` lays out.
    std::uint64_t frameStateBytes = 0u;

    /// The local-weight grids of every frame, at `kLocalWeightNodeBytes` per node.
    std::uint64_t localWeightGridBytes = 0u;

    /// What the caller stated it already holds.
    std::uint64_t preparedStateBytes = 0u;

    /// The fitted local-normalization document's reserved residency, owned outside StackWorkingSet.
    std::uint64_t fittedNormalizationBytes = 0u;

    /// The sum of the persistent figures above, including prepared and fitted state.
    std::uint64_t persistentBytes = 0u;

    /// What one worker holds while combining one pixel's column of samples and while reading a
    /// band through the local surface, which is `stackScratchBytesPerWorker`. Zero under a
    /// streaming strategy that applies no local normalization.
    std::uint64_t scratchBytesPerWorker = 0u;

    /// How many workers the scratch is sized for: the concurrency the operation was admitted.
    std::uint32_t workers = 0u;

    /// The per-worker figure times the workers.
    std::uint64_t scratchBytes = 0u;

    /// What one row of the tile costs: every frame's samples for that row at the tile width under
    /// sample history and under region access, where every frame's band of a tile is held at once;
    /// under whole-frame access, one frame's samples for that row at the tile width for a streaming
    /// strategy whose storage is not the tile width, because the accumulators read the tile width
    /// and a stored integer row has to be widened somewhere, and zero for one over frames stored at
    /// the tile width, whose read buffer is what the accumulators read. Plus, when the stack applies
    /// a local weight surface, `stackLocalWeightTileBytesPerRow`: the surface's weight at every
    /// pixel of the row, for every frame under sample history or region access and for one frame
    /// otherwise.
    std::uint64_t tileBytesPerRow = 0u;

    /// The per-row figure times the tile's rows.
    std::uint64_t tileBytes = 0u;

    /// The decoded input held during the combine pass: whole frames under whole-frame access and
    /// bands under region access, times the concurrent reads.
    std::uint64_t readBufferBytes = 0u;

    /// The largest workspace any measurement pass needs beyond the persistent state and one
    /// whole frame as stored. A measurement reads the frame as the combine will, through the
    /// source path, so the workspace holds one frame at the tile width when the storage is not
    /// that width; under a local normalization scope it also holds the reference frame at the
    /// tile width, which the fit reads beside every other frame; and the local-weight measurement
    /// adds its integral images. Zero when no measurement pass is planned.
    std::uint64_t measurementWorkspaceBytes = 0u;

    /// Peak scratch allocated by composed normalization operations during MeasureNormalization.
    /// Included in that pass and its budget; allocated by the normalization operation itself.
    std::uint64_t normalizationScratchBytes = 0u;

    /// Peak scratch allocated by rejection diagnostics during Finalize, included in its budget.
    std::uint64_t diagnosticsScratchBytes = 0u;

    /// Scratch held while deriving frame weights before the first pass.
    std::uint64_t initializationScratchBytes = 0u;

    /// The largest working set of initialization or any pass.
    std::uint64_t peakBytes = 0u;

    /// What the plan was allowed to reach: the budget less what the engine context already had
    /// outstanding, or the caller's limit when it is smaller.
    std::uint64_t availableBytes = 0u;

    /// The bound the plan was held to: the engine context's configured budget or the caller's
    /// limit, whichever is smaller.
    std::uint64_t budgetBytes = 0u;

    /// Whether anything bounded the working set at all.
    bool budgetBounded = false;

    /// Whether the three output figures cover one tile's rows rather than the whole output: the
    /// caller takes each tile's output as the tile is finished, and none of it is handed over.
    bool outputCoversOneTile = false;

    /// Whether two plans account the same bytes.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackMemoryPlan& left,
                                                   const StackMemoryPlan& right) noexcept = default;
};

/// What is read, and how many times.
struct StackIoPlan
{
    /// How the source reads one frame.
    StackSourceAccess access = StackSourceAccess::WholeFrame;

    /// How many frames the source may hold decoded at once.
    std::uint32_t concurrentFrameReads = 1u;

    /// How many passes read the input: the measurements that are planned plus the combine.
    std::uint32_t inputPasses = 0u;

    /// How many times a frame is opened over the whole run, summed over every frame.
    std::uint64_t frameReads = 0u;

    /// The bytes of one decoded frame at its stored representation.
    std::uint64_t frameBytes = 0u;

    /// The bytes of every frame read once.
    std::uint64_t distinctInputBytes = 0u;

    /// The bytes the run reads, counting every re-read.
    std::uint64_t logicalInputBytes = 0u;

    /// How many times each frame is opened during the combine pass: the tile count under sample
    /// history or region access, one otherwise.
    std::uint32_t readsPerFrame = 1u;

    /// How many times the combine pass reads the whole input set: the tile count when sample
    /// history is read from whole frames, one otherwise. Above one is the figure the warning
    /// diagnostic carries.
    std::uint32_t inputMultiplier = 1u;

    /// Whether two plans read the same way.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackIoPlan& left,
                                                   const StackIoPlan& right) noexcept = default;
};

/// Which implementation the kernels will dispatch to, and on how many threads.
struct StackBackendPlan
{
    /// What the caller asked for.
    ComputeBackend requestedBackend = ComputeBackend::Automatic;

    /// What the dispatch table answered.
    BackendSelection selection{};

    /// Whether a named backend could not be served and the selection is a fallback.
    bool fellBack = false;

    /// The concurrency the operation was admitted, which is what the tile loop runs on.
    std::uint32_t workers = 0u;

    /// Which reductions must be thread-count independent.
    ReductionDeterminism reductionDeterminism = ReductionDeterminism::DecisionFeeding;

    /// Whether two plans state the same backend.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackBackendPlan& left,
                                                   const StackBackendPlan& right) noexcept = default;
};

/// How a stack will run.
///
/// A plain value built by `planStack` and inspected field by field. An executor follows it; a
/// caller reads it to say what the run will cost before it starts.
struct StackPlan
{
    /// Where the samples are while they combine.
    StackStrategyPlan strategy{};

    /// Which width each stage runs in.
    StackPrecisionPlan precision{};

    /// How the frame is cut.
    StackTilePlan tile{};

    /// The passes, in order.
    StackPassPlan passes{};

    /// Every buffer, by name, against the budget.
    StackMemoryPlan memory{};

    /// What is read, and how many times.
    StackIoPlan io{};

    /// Which implementation runs.
    StackBackendPlan backend{};

    /// Whether two plans are the same.
    ///
    /// Not `noexcept`, because the pass plan's comparison is not.
    ///
    /// @param left one plan.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackPlan& left, const StackPlan& right) = default;
};

} // namespace an

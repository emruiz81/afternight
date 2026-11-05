// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the state one stack execution holds, and the stages of `executeStack` that
// borrow it.
//
// `executeStack` builds one `StackExecutionState` on its own stack frame and hands it by reference
// to four units, one per stage: `StackExecutionInput.cpp` reads frames through the source and
// converts them through the source path, `StackExecutionMeasurement.cpp` runs the normalization
// and local-weight measurement passes and prepares the frame weights, `StackExecutionCombine.cpp`
// runs the combine pass, and `StackExecutionReport.cpp` runs the finalization and composes the
// report's summaries. The state owns nothing the plan did not count: the working set owns every
// buffer, and the spans and record pointers below are cut from its frame-state block once, by
// `cutBookkeeping`, so they live exactly as long as the working set. The caller's objects - the
// engine context, the execution, the model, the source, the request and the outputs - are
// borrowed for the call.
//
// Progress is one unit per frame read plus one for the finalization. A stage wraps each unit's
// nested operations in a `ProgressRange` of one unit and calls `completeUnit` outside it, so every
// report lands on the scale the caller sees.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/LocalWeights.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackPlanningRules.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace an::stacking
{
/// The operation name the executor's own failures and diagnostics carry; a composed operation's
/// failure keeps its own.
inline constexpr const char* kExecute = "an::executeStack";

/// The operation name a failure of `stackTransition` carries.
inline constexpr const char* kTransition = "an::stackTransition";

/// What a `printf` format's `%llu` wants a sixty-four-bit count as.
using Count = unsigned long long;

/// One frame's samples after the source path, wherever they sit.
struct StagedStackFrame
{
    /// The first sample.
    const void* samples = nullptr;

    /// Where the samples sit.
    const ImageLayout* layout = nullptr;

    /// What the samples are.
    SampleType sampleType = SampleType::Float32;

    /// Which samples mean something.
    ValidityDescriptor validity{};

    /// The packed bits, under the explicit-mask rule.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;

    /// Whether the samples sit in the executor's own memory and may be written.
    bool owned = false;
};

/// The coverage counts' running tallies, which a stack whose terms hold one tile folds in tile by
/// tile and every other stack gathers in one walk over the output. Every figure is an integer or a
/// sum of integers below 2^53, so the order the tiles or the chunks arrive in cannot change it.
struct StackCoverageTally
{
    /// Every count.
    double sum = 0.0;

    /// Every count squared.
    double squares = 0.0;

    /// The smallest count.
    std::uint32_t minimum = (std::numeric_limits<std::uint32_t>::max)();

    /// The largest count.
    std::uint32_t maximum = 0u;

    /// How many elements received a sample.
    std::uint64_t reached = 0u;

    /// How many elements were counted.
    std::uint64_t elements = 0u;
};

/// Everything one run holds, on the stack of `executeStack`.
///
/// The pointers to the caller's objects are set first and are never null afterwards; the figures
/// are derived from the model before the plan; the plan, the working set and the bookkeeping spans
/// are set once the plan exists. One thread drives the run; the composed operations it calls spread
/// their own work over the operation's workers.
struct StackExecutionState
{
    /// The engine context every planned buffer is taken from.
    const EngineContext* engine = nullptr;

    /// The operation the stack runs as.
    const ExecutionContext* execution = nullptr;

    /// The checked stack.
    const StackModel* model = nullptr;

    /// Where the frames come from, read one band at a time from the thread that drives the run.
    StackFrameSource* source = nullptr;

    /// The caller's facts about the run.
    const StackExecutionRequest* request = nullptr;

    /// The caller's records to fill.
    const StackExecutionOutputs* outputs = nullptr;

    /// The model with its normalization scope removed, which the measurement passes convert
    /// through: the coefficients they measure do not exist yet.
    std::optional<StackModel> preNormalization{};

    /// The caller's contract, amended with the document the stack fits.
    StackSourceContract contract{};

    /// The contract with only what precedes a normalization: the state, the plans, the masters.
    StackSourceContract preContract{};

    /// Which stages the source path runs on every band, as `checkStackSourceContract` derived them.
    StackSourceStages stages{};

    /// How the stack runs, as `planStack` decided it.
    StackPlan plan{};

    /// Every buffer the plan counted, returned to the engine context when the run ends.
    StackWorkingSet set{};

    /// The report being assembled, moved into the result on success.
    StackExecutionReport report{};

    /// The input's width, in pixels.
    std::uint32_t width = 0u;

    /// The input's height, in rows.
    std::uint32_t height = 0u;

    /// The input's channel count.
    std::uint32_t channels = 0u;

    /// How many frames the stack combines.
    std::uint32_t frames = 0u;

    /// The plan's worker count, which the scratch slices and the per-worker records are cut for.
    std::uint32_t workers = 1u;

    /// What the frames are stored as.
    SampleType inputType = SampleType::Float32;

    /// The tile width the source path brings them to: Float64 for Float64 storage, Float32
    /// otherwise.
    SampleType tileType = SampleType::Float32;

    /// The bytes of one stored sample.
    std::uint64_t inputSampleBytes = 0u;

    /// The bytes of one whole frame at the tile width, as `stackTileFrameBytes` counts them.
    std::uint64_t tileFrameBytes = 0u;

    /// Whether the stack combines tile sample history - the column kernels or the rejection
    /// estimators - rather than whole-output accumulators.
    bool history = false;

    /// Whether the terms hold one tile and each tile is finalized as soon as it is combined.
    bool tileTerms = false;

    /// The coverage folded in so far by a stack whose terms hold one tile.
    StackCoverageTally coverageTally{};

    /// What the finalization of the tiles combined so far wrote, for the same stack.
    StackFinalizeOutcome tileFinalize{};

    /// Whether the caller takes the output tile by tile: the working set holds one tile's output,
    /// written from its start for each tile, and each tile goes to the request's sink as soon as it is
    /// finished.
    bool outputByTile = false;

    /// For a stack that hands its output over tile by tile and writes the gain map: the positive gains
    /// of the tiles handed over so far, summed tile by tile in row order, and how many there were.
    double tileGainSum = 0.0;

    /// How many elements `tileGainSum` holds.
    std::uint64_t tileGainReached = 0u;

    /// Whether the stored representation is not the tile width, so a frame that is to be read at
    /// the tile width has to be converted into the executor's memory first.
    bool widens = false;

    /// Whether a streaming combine accumulates the frames' stored codes as they are stored: widening
    /// is the only stage and `stackAccumulatesStoredSamples` says it is the identity on their values,
    /// so the widened band would be the stored one written again.
    bool storedSamples = false;

    /// Whether the normalization scope has a local stage, alone or before a global one.
    bool localScope = false;

    /// A packed planar frame at the tile width.
    std::optional<ImageLayout> wholeTileLayout{};

    /// The description a frame has at the tile width.
    std::optional<ImageDescriptor> tileDescriptor{};

    /// Where each record sits in the frame-state block.
    StackFrameBookkeepingLayout bookkeeping{};

    /// One count per frame of the samples its rule admitted into the combine.
    std::span<std::uint64_t> admittedCounts{};

    /// One count per frame of the columns in which a rejection first rejected it.
    std::span<std::uint64_t> rejectedCounts{};

    /// The coverage histogram: one bin per count from zero to the frame count.
    std::span<std::uint64_t> histogram{};

    /// The rows a rejection gathers per-frame counts in before summing them in worker order; empty
    /// without a rejection.
    std::span<std::uint64_t> workerCounters{};

    /// One scratch slice per worker: the working set's scratch during the combine, a region of the
    /// measurement workspace during a local-weight measurement under a local scope.
    std::span<std::span<std::byte>> workerSlices{};

    /// One band per frame of the tile being combined, which the combination reads.
    StackFrameBand* bandRecords = nullptr;

    /// One mask view per frame beside the band records, which a band under the explicit-mask rule
    /// points at so the view outlives the read that produced it.
    ValidityMaskView<const std::uint8_t>* maskRecords = nullptr;

    /// The largest total of held planned buffers, fitted grids and one composed operation's scratch
    /// seen so far, which the report's peak takes when it exceeds the working set's own.
    std::uint64_t composedPeakBytes = 0u;

    /// One rejection rate per frame; null without a rejection.
    FrameRejectionRate* rateRecords = nullptr;

    /// The indices of the frames whose rejection rate stood out, in their leading entries; empty
    /// without a rejection.
    std::span<std::uint32_t> flaggedRecords{};

    /// Where the measurement passes carve their regions out of the workspace.
    StackMeasurementWorkspaceLayout workspaceLayout{};

    /// The local weight lattice, or nothing when the stack applies no local weighting.
    std::optional<LocalWeightGridShape> localWeightShape{};

    /// The bytes the lattice's measurement needs, as `localWeightMeasurementWorkspaceBytes` counts
    /// them; zero without a local weighting.
    std::uint64_t localWeightWorkspaceBytes = 0u;

    /// The document the stack fits when the caller supplies neither one nor a place for it.
    LocalNormalizationDocument fittedDocument{};

    /// The document in force for the combine, or null under a scope with no local stage.
    const LocalNormalizationDocument* document = nullptr;

    /// The progress total: the plan's frame reads and one for the finalization.
    std::uint64_t progressTotal = 0u;

    /// How many units of `progressTotal` are done.
    std::uint64_t progressCompleted = 0u;
};

/// Counts the local fit and optional diagnostics before the first source read.
///
/// Sets the planning request's fitted-document, normalization-scratch and diagnostics-scratch bytes
/// to what the composed operations will take, so the plan refuses a run over budget before a frame
/// is read; `ErrorCode::LimitExceeded` when a count overflows, or the local plan's refusal.
[[nodiscard]] Status planComposedMemory(const StackExecutionState& run, StackPlanningRequest& planning);

/// Records a composed operation's peak beside the currently held planned buffers and fitted grids.
void recordComposedMemory(StackExecutionState& run, std::uint64_t scratchBytes);

/// Reports one `printf`-formatted sentence of at most 511 bytes to the operation's diagnostic sink
/// under `kExecute`, and formats nothing when there is no sink.
void say(const ExecutionContext& execution, DiagnosticSeverity severity, const char* format, ...);

/// A read's failure as the stack's: the source's own code, a message naming the frame, the frame as
/// the `ContextKey::Requested` field, and the read's status as the cause.
[[nodiscard]] Status readFailure(std::uint32_t frame, const Status& cause);

/// Whether an address is one of a buffer's bytes; false for an empty buffer.
[[nodiscard]] bool inside(std::span<const std::byte> buffer, const void* pointer) noexcept;

/// Cuts the working set's frame-state block into the run's counters, histogram, worker slices and
/// records, constructing the records in place. Called once, after the working set exists.
void cutBookkeeping(StackExecutionState& run);

/// Points every worker's slice at the working set's scratch when `fromWorkspace` is empty, or at
/// consecutive per-worker regions of `fromWorkspace` otherwise.
void pointWorkerSlices(StackExecutionState& run, std::span<std::byte> fromWorkspace);

/// The first stored band of `rows` rows of the read buffer, which a read is lent unless it is
/// given a band of the tile instead.
[[nodiscard]] std::span<std::byte> readSlot(StackExecutionState& run, std::uint32_t rows);

/// Reads rows of one frame through the source into `slot` and checks what came back.
///
/// Cancellation is checked before and after the read. A failure of the read, or an exception it
/// throws, becomes the stack's through `readFailure`; a band of another representation, geometry,
/// frame or first row is `ErrorCode::IncompatibleArguments`, and one with no samples or layout or
/// with a mask that disagrees with its rule is `ErrorCode::InvalidArgument`, each naming `source`.
/// On success counts the read and its stored bytes in the report and sets `inSlot` to whether the
/// band's samples lie in `slot`. `kind` names the pass and does not change the read.
[[nodiscard]] Status readFrameInto(StackExecutionState& run, StackPassKind kind, std::uint32_t frame,
                                   std::uint32_t firstRow, std::uint32_t rows, std::span<std::byte> slot,
                                   StackSourceBand& band, bool& inSlot, bool counted = true);

/// `readFrameInto` lending the read the read slot.
[[nodiscard]] Status readFrame(StackExecutionState& run, StackPassKind kind, std::uint32_t frame,
                               std::uint32_t firstRow, std::uint32_t rows, StackSourceBand& band, bool& inSlot);

/// Marks one unit - a read and what was done with it, or the finalization - as done and reports
/// progress on the plan's scale under the pass's name. Called outside any nested range.
void completeUnit(StackExecutionState& run, StackPassKind kind);

/// Converts one band through the source path under `model` and `contract`, handing it the frame
/// normalization table when the model's scope has a global stage, and records whether a compiled
/// row ran.
[[nodiscard]] Result<StackSourceOutcome> convert(StackExecutionState& run, const StackModel& model,
                                                 const StackSourceContract& contract, const StackSourceBand& band,
                                                 const StackTileBand& tile,
                                                 std::span<const std::span<std::byte>> scratch);

/// Reads one frame whole and brings it to where a measurement reads it: through the whole model
/// when `whole` is true, through the model with no normalization scope otherwise. A frame no stage
/// applies to is left where the source put it. Otherwise a widened frame is converted into
/// `owned`, a frame the source decoded into the read slot is transformed in place, and a resident
/// frame is converted into the read slot.
[[nodiscard]] Status stageWholeFrame(StackExecutionState& run, StackPassKind kind, std::uint32_t frame, bool whole,
                                     std::span<std::byte> owned, std::span<const std::span<std::byte>> scratch,
                                     StackSourceBand& bandStorage, StagedStackFrame& staged);

/// A staged frame as the normalization operations read it, described at the tile width or at the
/// stored representation, whichever it now has.
[[nodiscard]] Result<NormalizationFrameView> normalizationView(const StackExecutionState& run,
                                                               const StagedStackFrame& staged);

/// Writes one frame's global match into the working set's table and, when the caller asked for
/// them, into `StackExecutionOutputs::frameNormalization`.
[[nodiscard]] Status recordMatch(StackExecutionState& run, std::uint32_t frame, const FrameNormalization& match);

/// Copies the contract's prepared global matches into the table before any pass runs, from the
/// local document when the contract carries one; `ErrorCode::IncompatibleArguments` naming
/// `contract` when a frame has no match.
[[nodiscard]] Status storePreparedMatches(StackExecutionState& run);

/// Fills the frame weight table - ones, the caller's explicit weights, or weights derived from the
/// request's measurements - and records what it holds in the report and the caller's outputs.
[[nodiscard]] Status prepareWeights(StackExecutionState& run);

/// Measures one frame that is not the reference: fitted against the reference into `document`
/// under a local scope, matched to `combined` under a global one, or fitted and then matched over
/// its locally normalized samples.
[[nodiscard]] Status measureNormalizationFrame(StackExecutionState& run, std::uint32_t frame,
                                               std::span<std::byte> frameSlot,
                                               const NormalizationFrameView& referenceView,
                                               const std::optional<NormalizationReference>& combined,
                                               LocalNormalizationDocument* document);

/// The normalization measurement pass: the reference frame first, then every other frame in order.
[[nodiscard]] Status measureNormalization(StackExecutionState& run);

/// The local-weight measurement pass: every frame's lattice measured through the whole model,
/// then the finished surface.
[[nodiscard]] Status measureLocalWeights(StackExecutionState& run);

/// The finalization targets over the working set's image and maps, with `maskView` as the mask.
[[nodiscard]] StackFinalizeTargets targetsOf(StackExecutionState& run, const ValidityMaskView<std::uint8_t>* maskView);

/// The targets one tile writes when the caller takes the output tile by tile: the working set's
/// image, mask and maps over the tile's rows, packed from their start. `maskView` receives the mask's
/// view over those rows, which the targets point at and which must outlive them.
[[nodiscard]] StackFinalizeTargets tileTargetsOf(StackExecutionState& run, std::uint32_t rows,
                                                 std::optional<ValidityMaskView<std::uint8_t>>& maskView);

/// Hands one finished tile to the request's sink: its coverage folded first, from its coverage map,
/// under sample history - a stack whose terms hold one tile folded it from the terms already - and
/// its gain from its gain map, so the summaries read what left; then the tile itself.
[[nodiscard]] Status handOverTile(StackExecutionState& run, const StackFinalizeTargets& targets, std::uint32_t firstRow,
                                  std::uint32_t rows);

/// Adds one tile's gain map into the run's gain tallies, chunked as the whole map's mean is.
[[nodiscard]] Status foldTileGain(StackExecutionState& run, std::span<const float> gain);

/// The working set's accumulator terms as one set.
[[nodiscard]] StackAccumulatorSet accumulatorsOf(StackExecutionState& run);

/// Fills one frame's local weights over a band of rows from its finished grid.
[[nodiscard]] Status fillWeights(StackExecutionState& run, std::uint32_t frame, std::uint32_t firstRow,
                                 std::uint32_t rows, std::span<float> weights);

/// Reads one frame's rows of a sample-history tile, brings them into the tile unless a resident
/// region can be borrowed as it is, fills its local weights, and records its band.
[[nodiscard]] Status readHistoryBand(StackExecutionState& run, std::span<std::byte> tile, const ImageLayout& bandLayout,
                                     std::uint64_t bandBytes, std::uint64_t weightsOffset,
                                     std::uint64_t weightBandBytes, std::uint32_t frame, std::uint32_t firstRow,
                                     std::uint32_t rows);

/// The combine pass under sample history: every frame's band of each tile, then the column kernels
/// or the rejection estimators over the tile, writing `targets` directly.
[[nodiscard]] Status combineHistory(StackExecutionState& run, const StackFinalizeTargets& targets);

/// Accumulates one stored band of one frame into the whole-output terms, converting it first when
/// a source stage applies.
[[nodiscard]] Status accumulateBand(StackExecutionState& run, const StackAccumulatorSet& set,
                                    const StackSourceBand& stored, bool inSlot, std::uint32_t firstRow,
                                    std::uint32_t rows);

/// Reads one frame's rows of a streaming tile under region access and records the band the
/// accumulators will read, decoding straight into the tile when nothing transforms the samples.
/// A read made `together` with others installs no progress range and leaves the report's counts to
/// its caller.
[[nodiscard]] Status readStreamingBand(StackExecutionState& run, std::span<std::byte> tile,
                                       const ImageLayout& bandLayout, std::uint64_t bandBytes,
                                       std::uint64_t weightsOffset, std::uint64_t weightBandBytes, std::uint32_t frame,
                                       std::uint32_t firstRow, std::uint32_t rows, bool together = false);

/// Whether a streaming tile's bands are read several at once: a source that allows it, a request for
/// more than one read in flight, and reads that each land in their frame's own band of the tile.
[[nodiscard]] bool readsTogether(const StackExecutionState& run) noexcept;

/// Reads every frame's band of one streaming tile, up to the plan's concurrent reads at once on the
/// operation's workers; a failure is the first in frame order.
[[nodiscard]] Status readStreamingBandsTogether(StackExecutionState& run, std::span<std::byte> tile,
                                                const ImageLayout& bandLayout, std::uint64_t bandBytes,
                                                std::uint64_t weightsOffset, std::uint64_t weightBandBytes,
                                                std::uint32_t firstRow, std::uint32_t rows);

/// The streaming combine pass under region access: every frame's band of a tile folded into the
/// terms at once, and each tile finalized as it is combined when the terms hold one tile.
[[nodiscard]] Status combineStreamingRegion(StackExecutionState& run, const StackAccumulatorSet& set);

/// The combine pass under whole-output accumulators, by region or by whole frame as the plan reads.
[[nodiscard]] Status combineStreaming(StackExecutionState& run);

/// Folds coverage counts into the run's histogram and into `tally`, with the same result at every
/// worker count.
[[nodiscard]] Status foldCoverage(StackExecutionState& run, std::span<const std::uint32_t> counts,
                                  StackCoverageTally& tally);

/// Fills the report's coverage summary from the counts the stack kept.
[[nodiscard]] Status summarizeCoverage(StackExecutionState& run);

/// Fills the report's effective frame count and gains from what the stack kept.
[[nodiscard]] Status summarizeIntegration(StackExecutionState& run);

/// Fills the report's per-frame rejection summary, frame outliers and, when asked, rejection
/// clusters; a rejection that admitted nothing leaves them unsummarized rather than failing.
[[nodiscard]] Status summarizeRejection(StackExecutionState& run);

/// The finalization pass: a streaming stack's terms into the output unless every tile was already
/// finalized, then the coverage, integration and rejection summaries.
[[nodiscard]] Status finalize(StackExecutionState& run, const ValidityMaskView<std::uint8_t>* maskView);

} // namespace an::stacking

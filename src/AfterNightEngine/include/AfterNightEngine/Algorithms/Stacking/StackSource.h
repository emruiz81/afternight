// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a stack's frames are on the way in, and the one pass that reads a band of them.
///
/// A stack's input is a statement and a pass. `StackSourceContract` states what
/// every frame of the stack is - raw sensor codes, calibrated samples or samples already brought to
/// the local reference - and what brings it to the state the stack combines: the calibration plan
/// and its masters, the prepared global matches, the local-normalization document. Nothing is
/// inferred; a contract that names a stage the frames cannot take is refused by name, and
/// `checkStackSourceContract` is the closed table that says which stages a stack runs. Whether a
/// frame arrives registered, whether normalization was precomputed, whether a spill is needed is
/// either stated here or is not the stack's decision at all: a frame reaches the stack registered
/// because the source that produced it registered it, and a region read is the source's to offer
/// and the planner's to count.
///
/// `convertStackSourceBand` is the pass. It takes one band of one frame as stored - integer or
/// floating, planar or interleaved, whole frame or region - and writes the tile's band at the tile
/// width in one walk over the samples: widened under the frame's numeric domain, calibrated through
/// the plan with the masters' matching rows, transformed by the local surface and matched by the
/// global affine, each stage only when the contract states it. No frame is materialized in between.
/// Every stage narrows exactly where the operation it composes narrows, so the band holds the bits
/// the calibration executor, the local apply and the global apply produce in turn, and the tests
/// hold it to those three operations rather than to a second reading of their arithmetic. The
/// validity the band carries into the tile is stated too: calibration regenerates it as finite-only,
/// because a zero flat produces a quiet NaN; every other stage preserves the frame's claim.
///
/// The measurement passes read whole frames through the same pass into the workspace the planner
/// counts for them, so a global match is measured on the samples the combine will read and a local
/// surface is measured beside the reference it will be applied against.
///
/// A local-normalization document describes the plane it was measured on. Frames of the document's
/// own extent read it pixel for pixel; frames that lie on that plane enlarged - a drizzled stack's,
/// whose document was measured on the drizzle canvas's analysis plane - read it through a
/// `StackPlaneMapping`, every surface evaluated where the stack pixel lies on the document's plane.
///
/// Whether a frame is registered is the caller's statement rather than the stack's routing decision;
/// a per-frame dark optimization is a plan the caller prepares per frame; a master stating a mask,
/// zero or range rule is refused here and left to a materialized calibration; and a range rule is
/// refused under a normalization stage.

#pragma once

#include <AfterNightEngine/Algorithms/Calibration/ImageCalibration.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace an
{

class StackModel;

/// One operation's policy and its permission to run. Defined in `Runtime/ExecutionContext.h`.
///
/// Named here rather than included: the source path borrows one for the duration of a call, and a
/// caller reaching it holds an execution context and has therefore already included that header.
class ExecutionContext;

/// What has been done to a stack's frames before the stack reads them.
///
/// One state for every frame of one stack, because every frame of one stack is described alike.
/// The state decides which stages the source path may run: a raw frame may be calibrated, a
/// calibrated frame may not be calibrated again, and a frame already brought to the local
/// reference is not brought there twice.
enum class StackFrameState : std::uint8_t
{
    /// Sensor codes as the camera produced them, possibly debayered. Calibration is applied on the
    /// way in when the contract carries a plan and masters, and not at all when it does not - a
    /// master bias is a stack of raw frames with nothing applied.
    Raw,

    /// Bias, dark and flat have been applied. The normalization the model names is applied on the
    /// way in.
    Calibrated,

    /// The local-normalization surface has already been applied, so the local stage is skipped and
    /// only a global match the model names is applied on the way in.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    LocalNormalized
};

/// Whether a value names a state this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param state the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackFrameState(StackFrameState state) noexcept
{
    return static_cast<std::uint8_t>(state) <= static_cast<std::uint8_t>(StackFrameState::LocalNormalized);
}

/// The state's own name.
///
/// Thread-safe and allocation-free.
///
/// @param state the state to name. A value outside the enumeration is named `"UnknownStackFrameState"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackFrameStateName(StackFrameState state) noexcept;

/// Which stages the source path runs on every band of a stack, in the order it runs them.
///
/// Derived from the model and the contract by `checkStackSourceContract` and reported by every
/// conversion. A stack none of whose stages apply - frames stored at the tile width, calibrated,
/// combined as they are - reads its bands where they were decoded, and an executor that asks
/// `any()` first never copies them.
struct StackSourceStages
{
    /// Whether the stored representation is widened to the tile width, which is every storage but
    /// the tile width itself.
    bool convert = false;

    /// Whether bias, dark and flat are applied: raw frames with a plan and masters.
    bool calibrate = false;

    /// Whether the local-normalization surface is applied: a local scope over frames not already
    /// brought to the reference.
    bool normalizeLocally = false;

    /// Whether the global match is applied: a scope with a global method.
    bool normalizeGlobally = false;

    /// Whether every coefficient the stages need is supplied by the contract rather than measured
    /// by the stack: the document under a local stage, the matches under a global stage that no
    /// document carries. What the planner's `normalizationPrepared` takes.
    bool normalizationPrepared = true;

    /// Whether any stage runs, so a band has to be written at all.
    ///
    /// Allocation-free and usable in a constant expression.
    ///
    /// @return true when at least one stage runs.
    [[nodiscard]] constexpr bool any() const noexcept
    {
        return convert || calibrate || normalizeLocally || normalizeGlobally;
    }

    /// Whether two records state the same stages.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackSourceStages& left,
                                                   const StackSourceStages& right) noexcept = default;
};

/// Where a stack's frames lie on the plane its local-normalization document was measured on.
///
/// A document's grids span the frame they were fitted on, so without a mapping a document is
/// applied to frames of its own extent, pixel for pixel. A drizzled stack reads the drizzle canvas
/// - its frames enlarged by the drizzle's scale - while a normalization measured before they were
/// drizzled describes the canvas's analysis plane: the same rectangle before the scale, whose
/// extent is not the canvas's divided by it (`Algorithms/Drizzle/DrizzleCanvas.h`). A mapping
/// states where every stack pixel lies on the document's plane. Stack column `x`, whose centre is
/// at `x + 0.5` in engine pixel coordinates, lies at the plane's pixel index
/// `(x + 0.5 + offsetX) / scale - 0.5`, computed in Float64 in that order, and row `y` at
/// `(y + 0.5 + offsetY) / scale - 0.5`. Every surface is read there exactly as `evaluateLocalGrid`
/// reads it, a position before the plane's first pixel centre or past its last reading the nearest
/// edge, so the surfaces are exact at every stack pixel's centre and nothing is restated onto the
/// stack's extent; a scale of one with no offset reads the stack's own pixels, and a band stores the
/// bits it stores with no mapping.
///
/// The plane covers the frames when no stack pixel lies wholly outside it: the first column ends
/// past the plane's first edge, `offsetX + 1 > 0`, and the last column begins before its last edge,
/// `offsetX + width - 1 < planeWidth * scale`, and the rows likewise. Every drizzle canvas lies so
/// on its analysis plane however it was planned, whole or with its guard cropped first;
/// `drizzleLocalNormalizationPlane` (`Algorithms/Normalization/DrizzleLocalNormalization.h`) states
/// the mapping of a stack over a whole canvas.
///
/// A mapped document keeps the validity rule of the plane it was measured on, which need not be
/// the frames': a warped plane marks where no light reaches with an exact zero, a drizzled frame
/// with a quiet NaN under the finite-only rule. The mapping states the rule the frames carry into
/// the tile, and each band is checked against it instead of against the document's.
struct StackPlaneMapping
{
    /// How many stack pixels one pixel of the document's plane spans along each axis: finite and
    /// positive. A drizzle's scale.
    double scale = 1.0;

    /// How many columns of the enlarged plane lie before the stack's first, in stack pixels:
    /// finite. Zero for a stack over a whole drizzle canvas; its guard for one over the canvas a
    /// guard was cropped from.
    double offsetX = 0.0;

    /// How many rows of the enlarged plane lie before the stack's first, in stack pixels: finite.
    double offsetY = 0.0;

    /// The rule every frame carries into the tile - finite-only after calibration, the frame's own
    /// otherwise - which each band is checked against in place of the document's. A range rule is
    /// restated as finite-only before it reaches the tile, so a mapping that states one is refused.
    ValidityRule frameValidity = ValidityRule::AllValid;

    /// Whether two mappings place the frames alike under the same rule.
    ///
    /// @param left one mapping.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StackPlaneMapping& left,
                                                   const StackPlaneMapping& right) noexcept = default;
};

/// What every frame of a stack has in common on the way in, borrowed for the run.
///
/// A plain aggregate with an empty default for every member but the state, so a caller names what
/// it supplies. Nothing here is owned but the plane mapping, held by value: the plans, the masters,
/// the matches and the document are the caller's and must outlive every call that takes the
/// contract.
struct StackSourceContract
{
    /// What the frames are.
    StackFrameState state = StackFrameState::Calibrated;

    /// The calibration applied to raw frames: none, one plan every frame shares, or one plan per
    /// frame in frame order, each prepared against `stackSourceCalibrationDescriptor` and the
    /// same masters. Refused for any state but `StackFrameState::Raw`.
    std::span<const CalibrationPlan> calibration{};

    /// The masters every plan was prepared with, required exactly when `calibration` is not empty.
    /// Each is a Float32 or Float64 frame at the tile width stating an all-valid or finite-only
    /// rule; a master under a mask, zero or range rule is a materialized calibration's business.
    const CalibrationMasters* masters = nullptr;

    /// The prepared global matches, one per frame in frame order, under a global scope that no
    /// local stage precedes; empty when the stack measures them. Under a local stage the matches
    /// travel in the document's frame records and this must be empty.
    std::span<const FrameNormalization> normalization{};

    /// The prepared local-normalization document, under a local scope over frames not already
    /// brought to the reference; null when the stack fits it. Under a scope with no local stage,
    /// or over frames already brought to the reference, it must be null. It describes frames of the
    /// stack's extent, or the plane `localNormalizationPlane` places them on.
    const LocalNormalizationDocument* localNormalization = nullptr;

    /// Where the frames lie on the plane `localNormalization` was measured on, when that is not
    /// their own; nothing when the document was fitted on frames of the stack's extent. Under a
    /// mapping the document's shape is the plane's, which must cover the frames, and each band's
    /// rule is checked against the mapping's rather than the document's. Refused without a
    /// document.
    std::optional<StackPlaneMapping> localNormalizationPlane{};
};

/// Checks a contract against the model it serves, whole, and says which stages the stack runs.
///
/// A closed table, applied in a fixed order: the state; the calibration - refused for any state but
/// raw, one plan or one per frame, every plan valid and prepared against
/// `stackSourceCalibrationDescriptor` with the masters it names, every master at the tile width, of
/// the frames' extent, numeric domain, channels and CFA phase, under an all-valid or finite-only
/// rule; the prepared global matches - the model's method and estimator, the frames' channel count,
/// finite and non-zero coefficients, and the identity on the reference frame; and the document -
/// the model's channels and either its extent or, under a plane mapping, a plane that covers the
/// frames, a grid this build can hold, a known interpolation, finite grids of the shape's size, one
/// record per frame in order, positive finite scales, a reference index that is the model's
/// reference frame, and a post-local match on every frame exactly under
/// `StackNormalizationScope::LocalThenGlobal`; and the plane mapping - a document beside it, a
/// finite and positive scale, finite offsets and a rule the source path carries into the tile.
/// Which rule each frame states is checked per band, against the document's or the mapping's, when
/// the band is read.
///
/// Thread-safe with respect to everything it takes, which is only read. Allocates only a failure
/// it has to describe.
///
/// @param model the checked stack.
/// @param contract what the frames are and what brings them in.
/// @return the stages; `ErrorCode::InvalidArgument` for a state outside the enumeration, an
///         invalid plan, a null master pointer beside plans, a non-finite coefficient or a
///         non-positive scale, or a plane mapping's scale that is not finite and positive, offset
///         that is not finite or rule this build does not define, naming the member;
///         `ErrorCode::IncompatibleArguments` when a member does not describe this stack - plans
///         for a calibrated frame, a plan or master of another representation, extent, domain or
///         channel set, matches or a document of the wrong count, method, estimator, channels or
///         shape, a reference that is not the model's, matches beside a document, a document under
///         a scope with no local stage, a plane mapping without a document or whose plane leaves a
///         pixel of the frames wholly outside it - naming the member; and `ErrorCode::Unsupported`
///         for a master under a mask, zero or range rule, or a plane mapping stating a range rule.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackSourceStages>
checkStackSourceContract(const StackModel& model, const StackSourceContract& contract);

/// One frame's stored samples for a run of rows, borrowed for the call.
///
/// The band describes exactly the rows it holds: under region access the rows the source read,
/// under whole-frame access the whole frame or a window of it that `stackSourceRowsOf` cut. Its
/// layout may be planar, interleaved or mixed, at the stored representation; its validity is the
/// frame's own claim, with a mask covering the band's rows when the rule needs one.
struct StackSourceBand
{
    /// What the samples are: the model's input representation, as stored.
    SampleType sampleType = SampleType::Float32;

    /// The first byte of the band's samples, never null.
    const void* samples = nullptr;

    /// Where the samples sit: an extent of the frame's width, the band's rows and the frame's
    /// channel count, at the element width of `sampleType`. Never null.
    const ImageLayout* layout = nullptr;

    /// Which frame of the stack the band belongs to, below the model's frame count.
    std::uint32_t frameIndex = 0u;

    /// The frame row the band's first row is, so the band's rows read the masters' and the
    /// surface's matching rows. The band must end at or before the frame's last row.
    std::uint32_t firstRow = 0u;

    /// Which of the band's samples mean something: the frame's rule and, under the explicit-mask
    /// rule, its packed mask covering the band with one shared bit-plane or one per channel.
    StackBandValidity validity{};
};

/// A window of rows cut from a whole frame, with the storage its views need.
///
/// `stackSourceRowsOf` fills one; `band()` then describes the window as a source band that points
/// into this object, so the object must outlive the band and must not move while the band is used.
struct StackSourceRows
{
    /// The first byte of the window's samples.
    const void* samples = nullptr;

    /// The window's rows at the frame's strides.
    std::optional<ImageLayout> layout{};

    /// The window's rows of the frame's mask, when the frame's rule reads one.
    std::optional<ValidityMaskView<const std::uint8_t>> mask{};

    /// The frame's representation, index and rule, and the window's first row.
    StackSourceBand window{};

    /// The window as a source band.
    ///
    /// Allocation-free.
    ///
    /// @return a band whose layout and mask point into this object.
    [[nodiscard]] StackSourceBand band() const noexcept
    {
        StackSourceBand result = window;
        result.samples = samples;
        result.layout = layout.has_value() ? &*layout : nullptr;
        result.validity.mask = mask.has_value() ? &*mask : nullptr;
        return result;
    }
};

/// Cuts a window of rows from a whole frame.
///
/// The frame is a source band of every row of the frame; the window keeps its strides and its
/// mask's, so no sample moves. What the executor does under whole-frame access to read one tile's
/// rows of a decoded frame, and what a measurement does not need, because it reads the whole
/// frame.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param frame the whole frame, whose `firstRow` is zero.
/// @param firstRow the frame row the window begins at.
/// @param rows how many rows the window holds, at least one.
/// @return the window; `ErrorCode::InvalidArgument` for a null pointer, no rows, or a mask that
///         disagrees with the frame's rule; `ErrorCode::OutOfRange` when the window does not lie
///         inside the frame; `ErrorCode::IncompatibleArguments` when the frame's mask does not cover
///         the frame with one shared bit-plane or one per channel; and `ErrorCode::LimitExceeded`
///         when the window's extent, layout or mask cannot be described.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackSourceRows>
stackSourceRowsOf(const StackSourceBand& frame, std::uint32_t firstRow, std::uint32_t rows);

/// Where a band of the tile goes, borrowed for the call.
///
/// One frame's share of the tile: a packed planar band of the output's width and channels at the
/// tile width, which `stackTileBandLayout` describes and the accumulators and the column kernels
/// read. It may be the source band itself when the two representations agree, so a frame stored
/// at the tile width is normalized where it was decoded.
struct StackTileBand
{
    /// What the samples are: the plan's tile width.
    SampleType sampleType = SampleType::Float32;

    /// The first byte of the band's samples, never null.
    void* samples = nullptr;

    /// Where the samples sit: an extent of the frame's width, the band's rows and the frame's
    /// channel count, at the element width of `sampleType`. Never null.
    const ImageLayout* layout = nullptr;
};

/// The layout of one frame's band of the tile.
///
/// Packed and planar - rows of exactly the width, planes of exactly the rows, no padding - because
/// that is what the plan counted per row and what the compiled rows run over. The tile holds the
/// frames' bands in frame order, each `stackTileFrameBytes` long.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param model the checked stack.
/// @param rows how many rows the band holds, between one and the frame's height.
/// @return the layout; `ErrorCode::OutOfRange` for a row count outside the frame;
///         `ErrorCode::LimitExceeded` when the band's extent or layout cannot be described.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ImageLayout> stackTileBandLayout(const StackModel& model, std::uint32_t rows);

/// What reading one band through the source path did.
struct StackSourceOutcome
{
    /// Which stages ran.
    StackSourceStages stages{};

    /// How many samples the band wrote, which is every sample of the band.
    std::uint64_t samplesWritten = 0u;

    /// How many samples the frame's rule admitted and the stages transformed.
    std::uint64_t samplesTransformed = 0u;

    /// How many samples the frame's rule excluded and the pass carried unchanged - as their
    /// widened value, or as a quiet NaN under calibration.
    std::uint64_t samplesPreserved = 0u;

    /// How many excluded samples calibration wrote as a quiet NaN; a subset of the preserved.
    std::uint64_t invalidCalibrationInputs = 0u;

    /// How many admitted samples calibration left non-finite - a zero flat, a non-finite master
    /// sample - which the band's finite-only rule then excludes; a subset of the transformed.
    std::uint64_t nonFiniteOutputs = 0u;

    /// How many normalized samples landed exactly on zero under the zero rule and were moved to
    /// the smallest normal magnitude.
    std::uint64_t noDataCollisions = 0u;

    /// Which samples of the tile band mean something: finite-only after calibration or
    /// under a domain-range rule, whose excluded samples become quiet NaNs before
    /// conversion can change membership; the frame's rule otherwise.
    ValidityDescriptor validity{};

    /// Which implementation the dispatch table selected for the execution.
    BackendSelection backend{};

    /// Whether the band was written where it was read.
    bool inPlace = false;

    /// Whether a compiled row ran. False on the reference backend and on a build with no compiled
    /// row for the selected instruction set.
    bool compiledRowUsed = false;
};

/// Reads one band of one frame through the source path into the tile.
///
/// For every sample of the band: the frame's rule decides admission on the stored value; an
/// admitted sample is widened under the frame's numeric domain's conversion policy, calibrated
/// step by step with the plan's scalars and the masters' matching rows exactly as `calibrateImage`
/// calibrates it, transformed by the local surface exactly as `applyLocalNormalization` transforms
/// it - under a plane mapping with every surface read where the sample lies on the document's
/// plane, exactly as `evaluateLocalGrid` reads it there, and skipped on the document's reference
/// frame, which is not normalized - and matched exactly
/// as `applyGlobalNormalization` matches it, each stage only when the stages say so; an excluded
/// sample is written as its widened value, or as a quiet NaN under calibration or a domain-range
/// rule, which preserves the original range membership through rounding without a separate mask.
/// An admitted sample that is not finite is refused, and a calibration result that is not finite
/// is written and counted, because the band's finite-only rule excludes it downstream. A range
/// rule is refused under a normalization stage, as both applies refuse it, because the stated
/// range describes the untransformed values.
///
/// The band's rows and channels are spread across the execution's admitted workers, each row of
/// each channel written by exactly one of them, so the result is independent of the thread count.
/// The compiled row of the selected backend runs each row; the scalar row runs on the reference
/// backend, and the two store the same bits.
///
/// Thread-safe with respect to `execution`, the model, the contract, the source and the table,
/// which are only read; the tile band and the worker scratch are written and must not be shared.
/// Allocation-free except for a failure it has to describe.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack.
/// @param contract what the frames are and what brings them in, as `checkStackSourceContract`
///        accepts it; the members this band reads are checked again here.
/// @param source the frame's stored samples for the band.
/// @param frameNormalization the working set's per-frame coefficient table - an offset and a
///        scale per frame and channel, frame-major, the offset first - read only under a global
///        stage, when it must hold every frame's row; empty otherwise.
/// @param workerScratch one slice per admitted worker, at least `admittedConcurrency` of them,
///        each holding at least `stackScratchBytesPerWorker` of the model beginning on a
///        `kStackScratchAlignmentBytes` boundary, as `StackWorkingSet::workerScratch` hands them
///        out; the source path uses the last `stackSourceScratchBytesPerWorker` bytes of each.
///        Read only under a local stage; empty otherwise.
/// @param tile where the band goes, at the tile width, of the source band's rows.
/// @return what was read; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from the checkpoint;
///         `ErrorCode::UnsupportedBackend` when the execution is constrained to a backend this
///         build cannot serve; whatever `checkStackSourceContract` refuses of the members the band
///         reads; `ErrorCode::IncompatibleArguments` when the band is not at the stored
///         representation, its layout does not describe the frame's width and channels, its mask
///         does not cover it, the tile is not at the tile width or not of the band's rows, the
///         table does not hold every frame, a scratch slice is short or too few are given, or the
///         frame's rule is not the document's - the mapping's, under a plane mapping;
///         `ErrorCode::InvalidArgument` for a null pointer, a misaligned pointer, a mask under a
///         rule that reads none, no mask under the rule that needs one, or a non-finite table
///         entry; `ErrorCode::OutOfRange` when `frameIndex` or the band's rows are outside the
///         frame; `ErrorCode::Unsupported` for a range rule under a normalization stage;
///         `ErrorCode::Aliasing` when the tile partly overlaps the source; and
///         `ErrorCode::InvalidData` when an admitted sample is not finite, after which the tile
///         band holds a partial result.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackSourceOutcome>
convertStackSourceBand(const ExecutionContext& execution, const StackModel& model, const StackSourceContract& contract,
                       const StackSourceBand& source, std::span<const double> frameNormalization,
                       std::span<const std::span<std::byte>> workerScratch, const StackTileBand& tile);

/// Writes one frame's global match into the working set's per-frame coefficient table.
///
/// The table is the one `StackWorkingSet::frameNormalization` hands out: an offset and a scale
/// per frame and channel, frame-major, the offset first. The match must be the model's: its
/// method and estimator, the frames' channel count, finite coefficients and a non-zero scale, and
/// an offset of exactly zero under a multiplicative method.
///
/// Thread-safe with respect to the model and the match; the table row is written and must not be
/// shared. Allocation-free except for a failure it has to describe.
///
/// @param model the checked stack, whose scope names a global method.
/// @param frameIndex which frame, below the model's frame count.
/// @param match the frame's match.
/// @param table the working set's table, of exactly two entries per frame and channel.
/// @return success; `ErrorCode::IncompatibleArguments` when the model names no global method, the
///         table is not the model's, or the match's method, estimator or channel count is not the
///         model's; `ErrorCode::InvalidArgument` for a non-finite coefficient, a zero scale or a
///         non-zero offset under a multiplicative method; `ErrorCode::OutOfRange` for a frame
///         outside the stack.
[[nodiscard]] AFTERNIGHTENGINE_API Status storeStackFrameNormalization(const StackModel& model,
                                                                       std::uint32_t frameIndex,
                                                                       const FrameNormalization& match,
                                                                       std::span<double> table);

/// Reads one frame's global match back out of the working set's per-frame coefficient table.
///
/// The inverse of `storeStackFrameNormalization`, for a caller recording what a frame was matched
/// by: the model supplies the method and the estimator the table does not carry, and the fallback
/// counts a measured match reports are not carried either and read as zero.
///
/// Thread-safe, reentrant, and allocation-free except for a failure it has to describe.
///
/// @param model the checked stack, whose scope names a global method.
/// @param frameIndex which frame, below the model's frame count.
/// @param table the working set's table, of exactly two entries per frame and channel.
/// @return the match; `ErrorCode::IncompatibleArguments` when the model names no global method or
///         the table is not the model's; `ErrorCode::OutOfRange` for a frame outside the stack.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameNormalization>
stackFrameNormalizationAt(const StackModel& model, std::uint32_t frameIndex, std::span<const double> table);

/// What fitting one frame's local coefficients into a document did.
struct StackLocalNormalizationFit
{
    /// The background fit; left at its default for the reference frame, which is not fitted again.
    LocalBackgroundFitResult background{};

    /// The scale estimate against the reference, absent for the reference frame.
    std::optional<LocalScaleEstimate> scale{};

    /// The scale surface fit, present when one was asked for and the frame is not the reference.
    std::optional<LocalScaleGridResult> scaleGrid{};

    /// Whether the frame is the document's reference, whose coefficients are the identity.
    bool isReference = false;
};

/// Starts a stack's local-normalization document from its reference frame.
///
/// Plans the request over the stack's geometry, fits the reference frame's background, and
/// records the request, the shape, the reference's rule, the reference index and its grid in the
/// document, with one record per frame of the stack whose background is empty until
/// `fitStackLocalNormalizationFrame` fills it. The reference's own record is filled here: its
/// background is the reference grid and its scale is one, so applying the document to it changes
/// nothing, which is what the source path's skip of the reference frame states.
///
/// Thread-safe with respect to `execution`, the model and the frame, which are only read; the
/// document is written and must not be shared. Allocates the document's grids and records, and
/// what the fit itself reports.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack, whose scope applies a local surface.
/// @param request what the surface is fitted under.
/// @param reference the reference frame read through the source path, at the tile width and the
///        stack's extent, under the rule every frame will state.
/// @param document receives the started document; whatever it held is replaced.
/// @return the reference's fit; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::IncompatibleArguments` when the model applies no local surface or the frame
///         is not the stack's geometry at the tile width; and whatever `planLocalNormalization`
///         and `fitLocalBackgroundGrid` refuse, including `ErrorCode::Cancelled` or
///         `ErrorCode::DeadlineExceeded` from the fit's checkpoints. The document is replaced only
///         on success.
[[nodiscard]] AFTERNIGHTENGINE_API Result<LocalBackgroundFitResult>
beginStackLocalNormalization(const ExecutionContext& execution, const StackModel& model,
                             const LocalNormalizationRequest& request, const NormalizationFrameView& reference,
                             LocalNormalizationDocument& document);

/// Fits one frame's local coefficients against a started document's reference and records them.
///
/// The frame's background is fitted, its scale estimated from the residual ratios against the
/// reference and, when asked, its scale surface fitted from them too; the record is written in
/// full or not at all. The reference frame is not fitted: its record was written when the
/// document was started, and asking for it reports that.
///
/// Thread-safe with respect to `execution`, the model and the two frames, which are only read;
/// the document's record for the frame is written and must not be shared. Allocates what the
/// fits report and the record's grids.
///
/// @param execution the operation the stack runs as. Must hold something.
/// @param model the checked stack, whose scope applies a local surface.
/// @param frame the frame read through the source path, at the tile width and the stack's extent,
///        under the document's rule.
/// @param frameIndex which frame, below the model's frame count.
/// @param reference the reference frame, as `beginStackLocalNormalization` received it.
/// @param fitScaleGrid whether to fit a scale surface beside the per-channel scale.
/// @param document the started document.
/// @return what was fitted; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidState` when the document was not started for this stack;
///         `ErrorCode::IncompatibleArguments` when the model applies no local surface or a frame
///         is not the stack's geometry at the tile width; `ErrorCode::OutOfRange` for a frame
///         outside the stack; and whatever the plan and the fits refuse, including
///         `ErrorCode::Cancelled` or `ErrorCode::DeadlineExceeded` from their checkpoints.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StackLocalNormalizationFit>
fitStackLocalNormalizationFrame(const ExecutionContext& execution, const StackModel& model,
                                const NormalizationFrameView& frame, std::uint32_t frameIndex,
                                const NormalizationFrameView& reference, bool fitScaleGrid,
                                LocalNormalizationDocument& document);

} // namespace an
